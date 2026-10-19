# Architecture

```
 host (Linux / WSL)                              STM32H753ZI (Nucleo)
 ------------------                              ----------------------------------------
 mkimage.py   app.bin --> app.img                0x08000000  bootloader (128 KB sector 0)
 flash.py  --- UART 115200 (ST-Link VCP) --->      main.c         boot decision, update loop
   HELLO / START / DATA / VERIFY / BOOT            session.c      frame -> updater -> reply   (pure)
   COBS frames, CRC32, ACK/NACK, retry             updater.c      IDLE/RECEIVING/VERIFIED     (pure)
                                                   protocol_codec COBS + packet CRC           (pure)
                                                   boot_check.c   header + CRC + vectors      (pure)
                                                   header.c crc32.c cobs.c                    (pure)
                                                 ------------------------------------------
                                                   port/ (hardware, thin)
                                                     transport_uart.c  USART3 RX ISR -> ring
                                                     flash_port.c      erase / 32 byte program
                                                     crc_hw_port.c     hardware CRC unit
                                                     board.c           clocks, GPIO, SysTick
                                                   boot.c             jump to application
                                                 0x08020000  image header (1 KB area, 28 B used)
                                                 0x08020400  application vector table + code
```

## Update sequence

```
host                       bootloader
 | -- HELLO ------------->  |  ACK(version, max chunk, slot size)
 | -- START(header) ----->  |  validate header; only then erase sectors  -> ACK   (seconds)
 | -- DATA(off, 256 B) -->  |  buffer to 32 B flash words, program        -> ACK   (x N)
 | -- VERIFY ------------>  |  CRC32 over flash (hardware unit) == header?
 |                          |    yes: program header word LAST             -> ACK
 |                          |    no : NACK VERIFY_FAIL, header stays erased
 | -- BOOT -------------->  |  ACK, re-check slot, jump
```

## Layering rule

Everything marked *pure* compiles and runs on the host (`tests/`), including a full host simulation
of the bootloader (`tests/sim_bootloader`) that the Python flasher is tested against. Hardware
access lives only in `bootloader/port/` and `bootloader/src/boot.c`. The transport is an interface
(`include/transport.h`); a CAN FD transport would implement the same three functions.
