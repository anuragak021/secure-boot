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
