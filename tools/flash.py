#!/usr/bin/env python3
"""Flash an image (made by mkimage.py) to the bootloader over the UART protocol."""

from __future__ import annotations

import argparse
import sys
import time
from typing import Protocol

import bl_protocol as bp
import imagefmt as im

DEFAULT_REPLY_TIMEOUT_S = 1.0
START_TIMEOUT_S = 20.0  # START erases flash sectors; the CPU is stalled for seconds
RETRY_LIMIT = 3
HELLO_INTERVAL_S = 0.05
HELLO_DRAIN_S = 0.15
FIXED_HEADER_BYTES = im.HEADER_BYTES


class FlashError(RuntimeError):
    """Update failed; the message says why."""


class Link(Protocol):
    """The subset of pyserial.Serial the flasher uses (lets tests supply a fake)."""

    def write(self, data: bytes) -> int | None: ...
    def read(self, size: int = 1) -> bytes: ...
    def reset_input_buffer(self) -> None: ...


class Flasher:
    def __init__(
        self,
        link: Link,
        log=print,
        reply_timeout: float = DEFAULT_REPLY_TIMEOUT_S,
        start_timeout: float = START_TIMEOUT_S,
    ) -> None:
        self.link = link
        self.log = log
        self.reply_timeout = reply_timeout
        self.start_timeout = start_timeout
        self.seq = 0
        self._rx = bytearray()

    # --- framing -------------------------------------------------------------------------
    def _read_frame(self, deadline: float) -> bytes | None:
        """Next complete frame (without delimiter), or None at the deadline."""
        while True:
            idx = self._rx.find(b"\x00")
            if idx >= 0:
                frame = bytes(self._rx[:idx])
                del self._rx[: idx + 1]
                if frame:
                    return frame
                continue
            if time.monotonic() >= deadline:
                return None
            chunk = self.link.read(1)
            if chunk:
                self._rx += chunk

    def _next_packet(self, deadline: float) -> bp.Packet | None:
        while True:
            frame = self._read_frame(deadline)
            if frame is None:
                return None
            try:
                return bp.decode_frame(frame)
            except bp.ProtocolError:
                continue  # noise, e.g. the app's text output: skip it

    # --- request/response ---------------------------------------------------------------
    def exchange(self, cmd: int, payload: bytes = b"", timeout: float | None = None) -> bytes:
        """Send one packet and wait for its ACK. Resend on timeout or NACK(BAD_CRC)."""
        seq = self.seq
        timeout = self.reply_timeout if timeout is None else timeout
        frame = bp.encode_packet(cmd, seq, payload)
        for attempt in range(1, RETRY_LIMIT + 2):
            self.link.write(frame)
            deadline = time.monotonic() + timeout
            retry_reason = "timeout"
            while True:
                pkt = self._next_packet(deadline)
                if pkt is None:
                    break
                if pkt.seq != seq or pkt.cmd not in (bp.CMD_ACK, bp.CMD_NACK):
                    continue  # stale reply from an earlier attempt
                if pkt.cmd == bp.CMD_ACK:
                    self.seq = (seq + 1) & 0xFF
                    return pkt.payload
                code = pkt.payload[0] if pkt.payload else 0
                name = bp.NACK_NAMES.get(code, f"code {code}")
                if name != "BAD_CRC":
                    raise FlashError(f"bootloader rejected command 0x{cmd:02X}: NACK {name}")
                retry_reason = "NACK BAD_CRC"
                break
            self.log(f"  retry {attempt}/{RETRY_LIMIT} for cmd 0x{cmd:02X} ({retry_reason})")
        raise FlashError(f"no ACK for command 0x{cmd:02X} after {RETRY_LIMIT} retries")

    # --- session ------------------------------------------------------------------------
    def hello(self, wait_s: float) -> bytes:
        """Send HELLO until the bootloader answers (press RESET meanwhile)."""
        deadline = time.monotonic() + wait_s
        seq = 0
        last_ack = None
        reply = b""
        while time.monotonic() < deadline:
            self.link.write(bp.encode_packet(bp.CMD_HELLO, seq))
            end = time.monotonic() + HELLO_INTERVAL_S
            while True:
                pkt = self._next_packet(end)
                if pkt is None:
                    break
                if pkt.cmd == bp.CMD_ACK:
                    last_ack, reply = pkt.seq, pkt.payload
            if last_ack is not None:
                break
            seq = (seq + 1) & 0xFF
        if last_ack is None:
            raise FlashError(
                f"no answer to HELLO within {wait_s:.0f} s: is the board running the "
                "bootloader? Press RESET (or hold B1 while resetting) and retry"
            )
        # Several HELLOs may have been in flight; the last ACK tells where numbering stands.
        end = time.monotonic() + HELLO_DRAIN_S
        while True:
            pkt = self._next_packet(end)
            if pkt is None:
                break
            if pkt.cmd == bp.CMD_ACK:
                last_ack, reply = pkt.seq, pkt.payload
        self.seq = (last_ack + 1) & 0xFF
        return reply

    def update(
        self,
        image: bytes,
        boot: bool = True,
        flip_payload_byte: int | None = None,
        abort_after_packets: int | None = None,
    ) -> dict[str, float]:
        header, payload = im.split_image(image)  # header bytes are sent exactly as stored
        if flip_payload_byte is not None:  # test aid: corrupt the data after the header CRC
            buf = bytearray(payload)
            buf[flip_payload_byte] ^= 0x01
            payload = bytes(buf)

        t0 = time.monotonic()
        self.log("START (erasing flash, this takes a few seconds)")
        self.exchange(bp.CMD_START, header, timeout=self.start_timeout)
        t_data = time.monotonic()
        sent = 0
        total = len(payload)
        packets = 0
        next_report = 10
        for off in range(0, total, bp.MAX_DATA_BYTES):
            chunk = payload[off : off + bp.MAX_DATA_BYTES]
            self.exchange(bp.CMD_DATA, off.to_bytes(4, "little") + chunk)
            sent += len(chunk)
            packets += 1
            if abort_after_packets is not None and packets >= abort_after_packets:
                raise FlashError(f"test abort after {packets} DATA packets ({sent} bytes)")
            if sent * 100 // total >= next_report:
                self.log(f"  {sent * 100 // total:3d}%  {sent}/{total} bytes")
                next_report += 10
        t_done = time.monotonic()
        self.log("VERIFY")
        self.exchange(bp.CMD_VERIFY, timeout=self.reply_timeout * 5)
        if boot:
            self.log("BOOT")
            self.exchange(bp.CMD_BOOT)
        t_end = time.monotonic()
        data_s = max(t_done - t_data, 1e-9)
        return {
            "payload_bytes": float(total),
            "total_s": t_end - t0,
            "erase_s": t_data - t0,
            "data_s": data_s,
            "kb_per_s": total / 1024.0 / data_s,
        }


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("image", help="image file from mkimage.py")
    ap.add_argument("-p", "--port", default="/dev/ttyACM0")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("--wait", type=float, default=15.0, help="seconds to wait for the bootloader")
    ap.add_argument("--no-boot", action="store_true", help="verify but do not send BOOT")
    ap.add_argument("--no-local-check", action="store_true", help="skip image validation (tests)")
    ap.add_argument(
        "--test-flip-payload-byte", type=int, metavar="N", help="corrupt payload byte N"
    )
    ap.add_argument("--test-abort-after", type=int, metavar="N", help="stop after N DATA packets")
    args = ap.parse_args(argv)

    try:
        import serial

        with open(args.image, "rb") as fh:
            image = fh.read()
        if not args.no_local_check:
            hdr = im.check_image(image)
            print(f"image OK: {hdr.image_size} bytes, version 0x{hdr.fw_version:08X}")
        link = serial.Serial(args.port, args.baud, timeout=0.02)
    except (OSError, im.ImageError, ImportError) as exc:
        print(f"flash: error: {exc}", file=sys.stderr)
        return 1

    with link:
        fl = Flasher(link)
        try:
            print(f"waiting for bootloader on {args.port} (press RESET on the board)...")
            info = fl.hello(args.wait)
            bl_version = int.from_bytes(info[4:8], "little")
            print(f"bootloader found: proto {info[0]}, version 0x{bl_version:08X}")
            stats = fl.update(
                image,
                boot=not args.no_boot,
                flip_payload_byte=args.test_flip_payload_byte,
                abort_after_packets=args.test_abort_after,
            )
        except (FlashError, IndexError) as exc:
            print(f"flash: FAILED: {exc}", file=sys.stderr)
            return 2
    print(
        f"done: {stats['payload_bytes']:.0f} bytes, total {stats['total_s']:.2f} s "
        f"(erase {stats['erase_s']:.2f} s, data {stats['data_s']:.2f} s, "
        f"{stats['kb_per_s']:.2f} KB/s)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
