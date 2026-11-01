# secure-boot: STM32H7 UART firmware update bootloader

A bootloader for the STM32H753ZI (Nucleo-H753ZI) that receives a firmware image over UART,
verifies it with CRC32, writes it to flash and boots it. A corrupted or partial image is never
booted. Written in C11 (CMSIS, no HAL) with Python host tools.

**Not a secure boot yet.** There is no signature check: CRC32 detects corruption, not tampering.
Signing is the first item on the roadmap. The folder name is the project name.

## What was built and tested

| Area | What | Evidence |
|---|---|---|
| Bootloader | Boot decision (button B1, header/CRC/vector checks, 500 ms HELLO window), UART update loop, safe jump to app | runs on the board |
| Protocol | COBS framed, CRC32 protected packets, seq numbers, ACK/NACK, duplicate handling, host retries | host + hardware tests |
| Integrity | Image CRC32 with the STM32 hardware CRC unit, self test (`"123456789"` = `0xCBF43926`) at every boot; header written last | host + hardware tests |
| Host tools | `mkimage.py` (header builder), `flash.py` (flasher), `hwtest.py` (hardware-in-the-loop) | below |
| Demo apps | `fast` (LD1, 100 ms) and `slow` (LD3, 1000 ms) blink apps linked at `0x08020400` | updated back and forth on the board |

### Tests

* **50 C test functions** in 6 executables (CRC, COBS, header, protocol codec, updater state machine with a fake H7 flash, boot check), built with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wdouble-promotion -Werror` plus ASan/UBSan, run by `ctest`.
* **30 Python tests**: 14 unit tests of the image format, COBS, packets and `mkimage.py`, and 16 tests of `flash.py` and its live trace, of which 10 run it end to end against `sim_bootloader` (the actual C framing/session/updater code over pipes). Those 10 cover a normal update, a non-word-aligned size, a damaged frame, a lost frame, a flipped payload byte, a corrupt header, a killed transfer, a link fault injected at 88% of the transfer, `--no-boot` and the `--live` trace.
* **10 hardware scenarios** on the Nucleo (`tools/hwtest.py`, report in [`docs/hw_test_results.md`](docs/hw_test_results.md)), all passing on the final firmware:
  fast to slow and back, flipped payload byte, wrong magic / oversized image / bad header CRC, killed transfer then reset, duplicate / out-of-order / wrong-seq packets, throughput.
* Confirmed by eye on the board: after `flash.py` updated fast to slow, LD3 (red) blinks about once per second and LD1/LD2 are off; with B1 held at reset the bootloader stays in update mode.
* Also done by hand: flipping one byte in a real image file and flashing it with `--no-local-check` gives `NACK VERIFY_FAIL` (exit code 2), and the board stays in the bootloader after reset.
* `cppcheck` (warning, style, performance, portability; bootloader, app_demo, smoke, tests) and `clang-tidy` (bugprone, cert, clang-analyzer; pure-logic files through the host build, hardware files through the ARM compile database) are clean. Vendor CMSIS is excluded. `clang-format`, `black` and `ruff` checks pass. Deviations: see NOTES.md.

### Measured (115200 baud, 256 byte DATA packets, one ACK per packet; KB = 1024 bytes)

| Image | Total | Flash erase | Data transfer | Throughput |
|---|---|---|---|---|
| 1.5 KB (demo app) | about 0.9 s | about 0.8 s | about 0.15 s | about 9.5 KB/s |
| 32 KB | 4.20 s | 0.80 s | 3.38 s | 9.47 KB/s |
| 128 KB | 15.29 s | 1.55 s | 13.67 s | 9.36 KB/s |

Single runs on one board, not averages. The raw line rate (8N1) is 11,520 bytes/s = 11.25 KB/s,
so the per-packet ACK, COBS and CRC overhead costs roughly 17%. The bootloader uses about 6 KB
(text + data 6064 bytes) of its 128 KB sector.

## Quick start

```
sudo apt install gcc-arm-none-eabi cmake ninja-build openocd clang-format clang-tidy cppcheck
pip install -r requirements.txt black ruff

# host tests
cmake -S . -B build-host -G Ninja -DBUILD_HOST_TESTS=ON && cmake --build build-host
ctest --test-dir build-host --output-on-failure

# firmware
cmake -S . -B build-arm -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake_arm_toolchain.cmake
cmake --build build-arm        # bootloader.bin, app_fast.img, app_slow.img

# program the bootloader and the first app (ST-Link, OpenOCD)
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
  -c "program build-arm/bootloader/bootloader verify" \
  -c "program build-arm/app_demo/app_fast.img 0x08020000 verify" -c "reset run" -c exit

# update over UART, then press RESET (the tool waits for the bootloader)
python3 tools/flash.py build-arm/app_demo/app_slow.img
```

Add `--live` for a colour-coded, timestamped trace of every packet (command, sequence number, flash address, progress bar, NACK reasons). Packets are decoded on the host from the real wire traffic, and the bootloader's own text log lines (`BL: start`, `BL: HELLO received`, ...) are shown as `MCU log` lines. Handy for screen recordings. By default `--live` pauses 250 ms after each of the first 20 packet lines (5 s in total, `--live-pace 0` turns it off) so the two arrow directions are readable on video; that pacing is subtracted from the timings printed at the end and the line says so. `--reset` resets the board through OpenOCD first, so nobody has to press the button at the right moment.

For a longer demo transfer, `mkimage.py --pad-to` appends deterministic filler after the app. The app is unchanged and the filler is never executed; it only makes the transfer bigger:

```sh
python3 tools/mkimage.py build-arm/app_demo/app_slow.bin --version 2.0.0 --pad-to 262144 \
  -o build-arm/app_demo/app_slow_256k.img
python3 tools/flash.py --live build-arm/app_demo/app_slow_256k.img
```

Failure demo: `--test-fail-at 88` damages one DATA packet on the wire on every attempt once 88% of the image is sent. The bootloader answers `NACK BAD_CRC`, the flasher retries three times, then aborts with a red banner; the app is not booted and the header was never written. (A single flipped byte in the image, `--test-flip-payload-byte`, is only caught at VERIFY, after the whole transfer.)

```sh
python3 tools/flash.py --live --reset --test-fail-at 88 build-arm/app_demo/app_slow_256k.img
```

Measured on the board with this 256 KB padded image (one run): 31.1 s total, erase 2.3 s, data 28.7 s, 8.9 KB/s. The 1.5 KB image measured earlier is a different case: 9.9 KB/s, but dominated by the erase.

```sh
python3 tools/flash.py --live build-arm/app_demo/app_slow.img
```

The tool finds the Nucleo's virtual COM port by its stable `/dev/serial/by-id` name (`--port` to
override). Under WSL2 the ST-Link has to be attached first with `usbipd attach --wsl`.
Hold **B1** while pressing RESET to force the bootloader to stay in update mode.
LD2 (yellow) = update mode, LD3 (red) = idle heartbeat in update mode / error.

## Flash layout (2 MB, 2 banks, 128 KB sectors, 32 byte flash words)

| Region | Address | Size |
|---|---|---|
| Bootloader | `0x08000000` | 128 KB (sector 0) |
| Image header | `0x08020000` | 1 KB area, 28 bytes used (space reserved for a signature) |
| Application | `0x08020400` | rest of bank 1 (up to 895 KB) |
| Bank 2 | `0x08100000` | unused, reserved for a dual bank update |

Image header: `magic "SBOT"`, header version, firmware version, image size, image CRC32, flags,
header CRC32 (7 little endian u32). The header is programmed only after the image CRC matches,
so a partial transfer can never look valid.

## Protocol

Frame: `COBS( cmd u8 | seq u8 | len u16 | payload | crc32 u32 )` followed by `0x00`.
Commands: HELLO `01`, START `02`, DATA `03`, VERIFY `04`, BOOT `05`; replies ACK `80`, NACK `81`
with an error byte (BAD_CRC, BAD_LEN, BAD_SEQ, BAD_OFFSET, BAD_HEADER, FLASH_ERASE, FLASH_WRITE,
VERIFY_FAIL, NOT_READY, BAD_CMD). A DATA packet carries up to 256 image bytes. See
[`docs/architecture.md`](docs/architecture.md) and NOTES.md.

## Limitations (honest list)

* **No authentication.** CRC32 only. Anyone with UART access can load any image that has a valid CRC.
* **Single slot.** START erases the old app. If an update fails the device stays in the
  bootloader until a valid image arrives; there is no fallback to the previous image.
* **"Killed transfer" was tested by aborting the host and resetting the board**, not by cutting
  power. Real power loss during a flash program (partially programmed words, ECC errors on read)
  is untested.
* No read/write protection (RDP/WRP), no watchdog, no rollback protection.
* Throughput is limited by 115200 baud and one ACK per packet (no windowing).
* UART only. CAN FD is a design goal of the transport interface, not implemented.
* Verified on one Nucleo-H753ZI with CMSIS register access; no other board or HAL builds.

## Roadmap

1. ECDSA P-256 signature check (key in protected flash, signing in `mkimage.py`).
2. Dual bank update with bank swap, keeping the old image as fallback.
3. Watchdog based rollback: new app must confirm health or the bootloader reverts.
4. Anti-rollback version counter.
5. CAN FD transport (`transport_fdcan.c`), loopback first, then UDS style services.
6. ESP32 WiFi OTA bridge; Raspberry Pi Pico power-loss fault injection rig (hundreds of cuts, log the recovery rate).
7. RDP and write protection on the bootloader sector.
8. Reuse the COBS/CRC code for the brain UART link in `drosophila`.

## Repository layout

```
common/        image_header.h, protocol.h, bl_status.h, flash_layout.h (shared constants)
bootloader/    src/ (pure logic + main + jump), port/ (hardware), include/, linker/
app_demo/      fast and slow blink apps, linker script for the app slot
tools/         mkimage.py, flash.py, bl_protocol.py, imagefmt.py, hwtest.py, tests/
tests/         host unit tests, fake flash, sim_bootloader
third_party/   vendored CMSIS (ST device headers, startup, SystemInit) with licenses
smoke/         first hardware smoke test (LEDs + UART hello)
docs/          architecture.md, hw_test_results.md
NOTES.md       design decisions: what, why, tradeoffs, alternatives
```
