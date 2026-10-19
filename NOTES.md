# NOTES: design decisions

Plain-language record of what was done, why, and the alternatives. Updated as the work goes on.

## 1. Build environment (WSL2)

* The Nucleo is attached to WSL2 with `usbipd attach --wsl`. After that, Windows tools (STM32CubeProgrammer) cannot see the ST-Link, so **OpenOCD (in WSL) is the only flasher**. Tradeoff: OpenOCD 0.11 is older, but it probed the H753 (STM32H74x/75x, 2 MB, dual bank) fine.
* Two build trees: `build-host` (host unit tests, ctest) and `build-arm` (firmware, cross toolchain file).

## 2. CMSIS only, no HAL

* Hardware access is done with direct register access through the CMSIS device header (`third_party/cmsis`). Only the device header, ARM core headers, startup file and `SystemInit` are vendored (with their licenses).
* Why: the full HAL does not compile cleanly under `-Wconversion -Wpedantic -Werror`, pulls in a lot of code for a 128 KB bootloader, and its flash and cache helpers hide the exact sequence that this project is meant to demonstrate.
* Tradeoff: more register code to get right ourselves (UART, CRC unit, flash program/erase). Alternative: HAL in a relaxed-warning library, as the project spec allowed. Vendor code is built without `-Werror`, our own code with it.
* `SystemInit` resets RCC to HSI 64 MHz with all dividers at 1. Bootloader and app both stay on HSI with no PLL, so the clock state handed to the app is the reset state.

## 3. Pins (taken from the Cube Nucleo BSP header, not from memory)

* LD1 green PB0, LD2 yellow PE1, LD3 red PB14. Button B1 PC13. VCP is USART3 on PD8 (TX) and PD9 (RX), AF7.

## 4. Vector table alignment

* `startup_stm32h753xx.s` has 166 vector words = 664 bytes. Rounded up to a power of two that is 1024 bytes, so an app vector table at 0x08020400 (1 KB aligned) is valid. A `_Static_assert` / linker check is added with the app.

## 5. Smoke test (step 2)

* `smoke/main.c`: blinks LD1/LD2/LD3 in turn and prints a line on the VCP. Flashed with OpenOCD. Result: the UART line was received on `/dev/ttyACM0` at 115200. LED check pending the user.

## 6. Pure logic and host tests (step 3)

Modules (all pure C, no hardware): `crc32`, `cobs`, `header`, `protocol_codec`, `updater`. Tests are a small assert runner (`tests/test_util.h`), no external framework, run with ctest, built with the same strict warnings as firmware plus ASan/UBSan.

* **CRC32:** nibble table (16 entries) instead of the 256 entry table: 960 bytes less flash, slower. The packet CRC uses this software version; the hardware CRC unit is used only for the one-pass image check at VERIFY (see later section). Check value `0xCBF43926` for "123456789" is tested.
* **COBS:** encoder emits the minimal (reference) encoding, so a run of exactly 254 non zero bytes has no trailing code byte. Both forms decode correctly. Chosen so the Python side can compare bytes against a reference library.
* **Max payload deviation:** The project spec says max payload 256. A DATA payload is `offset(4) + data`, so I set the limit on the **image bytes per DATA packet = 256** (8 flash words) and the payload limit to 260. Alternative: 252 or 224 bytes of data to keep 256 total, but a 256 chunk maps cleanly onto flash words.
* **Header is explicit little endian** (`header_parse`/`header_serialize`), not a struct cast. No dependence on compiler padding, alignment or host endianness. The in-memory struct is just convenience.
* **Updater is a pure state machine** (IDLE, RECEIVING, VERIFIED). Flash ops (`erase`, `write`, `crc32_flash`) are injected through `flash_ops_t`, so the same code runs against `tests/fake_flash.c`, which enforces H7 rules (sector erase, 32 byte words, no reprogramming of a word).
* **Header written last:** the header word is programmed only after the image CRC matches. A killed transfer therefore leaves an erased (all 0xFF) header area, which fails validation. Tested (`test_killed_transfer_never_valid`).
* **Erase only after a sane header:** START validates the header first; a bad header does not touch flash (tested). Only the sectors covered by `0x400 + image_size` are erased.
* **Sequence handling:** a packet equal to the last accepted one (same seq and cmd) is re-ACKed without executing, which covers a lost ACK. Anything else must carry `last_seq + 1`. A NACKed packet does not advance the sequence, so the host retries it with the same seq. HELLO is accepted with any seq and resynchronises numbering. Tradeoff: an old duplicate that is no longer the last packet is rejected with BAD_SEQ rather than ignored.
* **Extra error code:** `BL_ERR_BAD_CMD` (10) for an unknown command; the documented enum had no code for it.
* **Failure policy:** any flash erase/write failure or VERIFY failure returns the updater to IDLE, so the host has to START again. Simple and safe, at the cost of not resuming.
* Tests: 43 test functions across 5 executables, all passing.

## 7. Image format, host tools and demo apps (step 4)

* **Image file = 0x400 byte header area + payload.** The header (28 bytes) sits at the start, the rest of the area is 0xFF. The file can therefore be programmed straight to 0x08020000 with OpenOCD (used to test the jump before the protocol exists), and `flash.py` sends the header bytes exactly as stored (never rebuilt) and the payload from file offset 0x400.
* **Header definition is duplicated** in `common/image_header.h` and `tools/imagefmt.py`. Both have a test with the same expected bytes (`test_serialize_layout` / `test_header_layout_matches_c`), and the same golden HELLO frame is checked in C and Python, so drift fails a test. Alternative: generate one from the other; not worth the build complexity for a 28 byte struct.
* **Firmware version** is `major<<16 | minor<<8 | patch`.
* **Python CRC** is `zlib.crc32`, the C side is the nibble table and (on target) the hardware unit; all three agree on `0xCBF43926`.
* **Demo apps:** `fast` = LD1 (green) 100 ms half period, version 1.0.0; `slow` = LD3 (red) 1000 ms, version 2.0.0. Different LED *and* speed, so a wrong image is obvious at a glance. Both print `app <name> running` on the VCP every 2 s, so progress can be checked from the host too.
* **App vector table:** linked at 0x08020400 with `ALIGN(1024)`; `SystemInit` is built with `USER_VECT_TAB_ADDRESS` + `VECT_TAB_OFFSET=0x20400` so `SCB->VTOR` is set at the very start of `Reset_Handler`. Linker `ASSERT`s check the origin is 1 KB aligned and the table (664 bytes) fits in 1 KB. Verified from the ELF: `.isr_vector` at 0x08020400, size 0x298.
* The bootloader keeps `SystemInit` without VTOR change (reset default 0x08000000).

## 8. Bootloader that validates and jumps (step 5), and the button bug

* **Hardware CRC (`crc_hw_port.c`):** poly 0x04C11DB7, init 0xFFFFFFFF, input reversal by *byte*, output reversal on, then XOR with 0xFFFFFFFF in software to match zlib. Bytes are written with 8 bit accesses so they are consumed in memory order (a 32 bit write would be consumed MSB first). A self test of "123456789" == 0xCBF43926 runs at every boot; if it fails the bootloader refuses to boot anything. Tradeoff: byte writes are slower than word writes, irrelevant at this image size. The image CRC is computed in one pass at VERIFY/boot time, never accumulated across packets.
* **Boot check (`boot_check.c`, pure, host tested):** header valid, image CRC matches, stack pointer inside AXI SRAM or DTCM and 8 byte aligned, reset vector has the Thumb bit and lies inside the image. Erased slot, a flipped payload byte, a flipped header byte and garbage vectors are all tested.
* **Jump (`boot.c`):** disable IRQs, stop SysTick and pulse the reset lines of every peripheral used (USART3, CRC, GPIOB-E), clear NVIC ICER/ICPR, clean+disable caches only if they were enabled (the bootloader never enables them), set VTOR, `DSB; ISB`, load MSP, branch. Interrupts stay disabled; the demo app calls `__enable_irq()` first. Without that the app would run with PRIMASK set, because a jump does not clear it the way a real reset does.
* **Bug found on hardware: button never read.** The H7 resets every GPIO pin to *analog* mode (MODER = 3), and in analog mode the input data register reads 0. My first version relied on "PC13 is an input after reset" (wrong). Found by reading `GPIOC->MODER` = 0xFFFFFFFF over SWD with OpenOCD. Fix: set PC13 to input and enable the internal pull-down (the Cube BSP does the same; B1 is active high). Verified with an SWD probe (button high while pressed) and by holding B1 during reset: the bootloader stays and blinks LD3 instead of jumping. Lesson: check pin modes explicitly, never assume reset defaults.
* **Flash driver (`flash_port.c`)** is only exercised from step 6 on (erase sector, program one 32 byte flash word per eight 32 bit stores). OpenOCD programmed the images in this step.
* **MISRA deviations:** integer-to-pointer casts for memory mapped flash reads/jump (`boot.c`, `crc_hw_port.c`, `flash_port.c`), unavoidable for register/flash access.

## 9. UART protocol on target, session module, host flasher (step 6)

* **RX path:** USART3 RX interrupt pushes bytes into a 1 KB ring buffer (power of two, index mask). The ISR is short: read RDR, store, advance head. Framing, COBS, CRC and the updater all run in the main loop. Single producer (ISR) / single consumer (main loop), so `volatile` indices plus a `__DMB()` are enough on this single core. Overrun/framing/noise flags are cleared in the ISR; if ORE were not cleared the receiver would stop for good.
* **Flash erase stalls the CPU:** the bootloader runs from bank 1 and erases bank 1, so instruction fetch stalls for the whole sector erase and no ISR runs. Bytes arriving then are lost (UART overrun). This is safe only because the host waits for the START ACK before sending DATA, and uses a long START timeout (20 s). Alternative: run the flash routines from RAM, or erase in bank 2 (that is the dual bank roadmap item).
* **`session.c`** (frame -> decode -> updater -> reply frame) is pure logic taking a send callback, so the exact code that runs on the MCU also runs in `tests/sim_bootloader` on the host.
* **Host simulator:** `sim_bootloader` is the real framing + session + updater + fake flash behind stdin/stdout. `tools/tests/test_flash_e2e.py` drives the real `flash.py` against it: exact image in flash, single flipped payload byte (rejected at VERIFY, header area still erased), corrupt header (rejected at START, nothing erased), killed transfer, damaged frames (retry through NACK BAD_CRC), lost frames (retry through timeout), and the unit-level HELLO timeout. This also proves the Python and C COBS/CRC/header code agree.
* **Retry policy (`flash.py`):** resend on timeout or NACK(BAD_CRC), at most 3 retries with the same seq. Any other NACK is fatal immediately (resending the same bytes would fail the same way), reported by name. Stale replies (wrong seq) are ignored.
* **HELLO sync:** the host sends HELLO every 50 ms while you press RESET. Several can be in flight when the first ACK arrives, so after the first ACK it listens 150 ms and continues from the last ACKed seq + 1.
* **Boot decision (`main.c`):** B1 held, invalid app or CRC self-test failure -> stay in update mode. Otherwise wait 500 ms for any valid packet; on silence boot the app. LD2 (yellow) on = update mode; LD3 (red) heartbeat while idle in update mode.
* **BOOT:** the app slot is re-validated (`boot_check_slot`) right before jumping, even after VERIFY succeeded, so what boots is what was just checked in flash, not what the state machine remembers.
* Dropped silently (host retries on timeout): frames with COBS errors or bad lengths. Answered with NACK(BAD_CRC): frames whose CRC fails, since cmd and seq are still readable.

## 10. Hardware results, tooling and what is NOT proven

* **Port numbers move.** When the FRDM i.MX 93 was plugged in, its debug UART (1a86 dual serial) took `/dev/ttyACM0` and the Nucleo moved to `ttyACM2`. My first flash attempt talked to the wrong device (it answered with a Linux login prompt). `flash.py` now finds the ST-Link by its `/dev/serial/by-id/*STLINK*-if02` name. Lesson: never hard-code `ttyACMx`.
* **`hwtest.py`** resets the board through OpenOCD and drives the real flasher, so the results in `docs/hw_test_results.md` are reproducible. It checks the bootloader's own serial output after each reset ("no valid app, staying" versus the app banner), not just the NACKs.
* **What "killed transfer" proves:** the host stops after N DATA packets and the board is reset. The slot was erased at START and the header was never written, so the boot check sees an erased header and stays in the bootloader. It does *not* prove behaviour under power loss.
* **Not proven (open risks):** (1) Power cut during a flash word program can leave a word with a bad ECC; reading it may fault. The boot check only reads the header word and then the payload if the header validates, and the header is the last word written, but I did not test this. (2) Flash erase runs while executing from the same bank (CPU stalls). It works on this board; a bank-2 or RAM-resident erase would be more robust. (3) Only the happy UART timing at 115200 was measured; no stress test with noise or long runs. (4) A run of single measurements, not statistics.
* **Static analysis:** cppcheck is clean on `bootloader/`, `app_demo/`, `smoke/` and `tests/`; clang-tidy is clean on the pure-logic files (host compile database) and on `main.c`, `boot.c`, `port/*.c`, `app_demo`, `smoke` (ARM compile database with `--target=arm-none-eabi` and the newlib include paths). Fixes made along the way: a dead store (cppcheck) and multiplication-based offsets (clang-tidy). Disabled check: `clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling` (it wants the C11 Annex K `memcpy_s`, which newlib does not have; all sizes are bounds checked by hand).
* **MISRA C:2012:** followed where practical, not formally checked (no MISRA tool). Known deviations: integer-to-pointer casts for flash/register access, the `noreturn` jump, vendor code.

## 11. What happens to the previous app (done-criteria item)

* **Single slot.** `START` erases the app sectors (only after the header passed validation, and only the sectors the new image needs). From that moment the old app is gone.
* **Failed or interrupted update:** the header word is programmed last, after the CRC matches. After a `VERIFY_FAIL`, a flash error, a host abort or a reset in the middle of a transfer the header area is still erased, so at the next reset `boot_check_slot` fails ("BL: no valid app, staying") and the device stays in the bootloader until a valid image arrives. Verified on hardware (`docs/hw_test_results.md`: flipped payload byte, killed transfer).
* **Rejected at START** (wrong magic, oversized image, bad header CRC): nothing is erased, the old app keeps booting. Also verified on hardware.
* **Not provided:** automatic fallback to the previous image. That needs two slots: dual bank with `SWAP_BANK` is roadmap item 2 (bank 2 at 0x08100000 is reserved for it).

## 12. Deliberately ignored return values

The standard says every return value is checked. These are `(void)` casts by design, each either harmless or covered by the host's retry:

* `send` failures on the reply path (`session.c`, `main.c`): a reply that does not leave the UART is indistinguishable from a lost reply, which the host handles by timeout and resend (the updater answers a repeated packet idempotently).
* `proto_rx_feed` returning `BL_ERR_OVERFLOW`: the receiver already dropped the over-long frame and resynchronised on the next delimiter; there is nothing else to do.
* `g_tp->init()` (UART): it can only return `BL_OK`; if the link were dead the bootloader would just be unreachable, which the LED heartbeat shows.
* `session_handle_frame` / `updater_process` results in the main loop: the reply (ACK or NACK) has already been sent; the status is the same information.
* `header_serialize` into a correctly sized local buffer (cannot fail for non-NULL arguments).
