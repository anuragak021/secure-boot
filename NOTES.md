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
