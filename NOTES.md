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
