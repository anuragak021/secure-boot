#!/usr/bin/env python3
"""Flash an image (made by mkimage.py) to the bootloader over the UART protocol."""

from __future__ import annotations

import argparse
import glob
import os
import re
import subprocess
import sys
import time
from typing import Protocol

import bl_protocol as bp
import imagefmt as im
import livelog

DEFAULT_REPLY_TIMEOUT_S = 1.0
START_TIMEOUT_S = 20.0  # START erases flash sectors; the CPU is stalled for seconds
RETRY_LIMIT = 3
OPENOCD_TIMEOUT_S = 20.0
HELLO_INTERVAL_S = 0.05
HELLO_DRAIN_S = 0.15
FIXED_HEADER_BYTES = im.HEADER_BYTES


# The bootloader also prints text lines ("BL: start" plus CR LF). They have no 0x00 delimiter,
# so they would be glued onto the next frame and destroy it.
TEXT_LINES = re.compile(rb"(?:[\x20-\x7e]*\r\n)+")

STLINK_PORT_GLOB = "/dev/serial/by-id/*STLINK*-if02"


def find_stlink_port() -> str:
    """Stable path of the Nucleo's virtual COM port (ttyACM numbers move when boards attach)."""
    matches = sorted(glob.glob(STLINK_PORT_GLOB))
    if len(matches) != 1:
        raise FlashError(
            f"expected exactly one ST-Link serial port ({STLINK_PORT_GLOB}), found {len(matches)}; "
            "use --port to choose"
        )
    return os.path.realpath(matches[0])


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
        trace=None,
        text=None,
        reply_timeout: float = DEFAULT_REPLY_TIMEOUT_S,
        start_timeout: float = START_TIMEOUT_S,
    ) -> None:
        self.link = link
        self.log = log
        self.trace = trace  # optional callback(direction, cmd, seq, payload) per packet
        self.text = text  # optional callback(line) for the bootloader's text log lines
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
                self._flush_text()
                return None
            chunk = self.link.read(1)
            if chunk:
                self._rx += chunk

    def _flush_text(self) -> None:
        """Hand any complete text log lines at the head of the buffer to the text callback."""
        m = TEXT_LINES.match(self._rx)
        if m:
            self._emit_text(bytes(self._rx[: m.end()]))
            del self._rx[: m.end()]

    def _emit_text(self, raw: bytes) -> None:
        if self.text:
            for line in raw.decode("ascii", "replace").splitlines():
                if line.strip():
                    self.text(line.strip())

    def _next_packet(self, deadline: float) -> bp.Packet | None:
        while True:
            frame = self._read_frame(deadline)
            if frame is None:
                return None
            try:
                pkt = bp.decode_frame(frame)
            except bp.ProtocolError:
                # Text log lines glued in front of a real frame: split them off, retry.
                m = TEXT_LINES.match(frame)
                if not m:
                    continue  # other noise: skip it
                self._emit_text(frame[: m.end()])
                try:
                    pkt = bp.decode_frame(frame[m.end() :])
                except bp.ProtocolError:
                    continue
            if self.trace:
                self.trace("RX", pkt.cmd, pkt.seq, pkt.payload)
            return pkt

    # --- request/response ---------------------------------------------------------------
    def exchange(
        self,
        cmd: int,
        payload: bytes = b"",
        timeout: float | None = None,
        corrupt: bool = False,
    ) -> bytes:
        """Send one packet and wait for its ACK. Resend on timeout or NACK(BAD_CRC).

        corrupt: test aid, damage the frame on the wire on every attempt (a dead link).
        """
        seq = self.seq
        timeout = self.reply_timeout if timeout is None else timeout
        frame = bp.encode_packet(cmd, seq, payload)
        if corrupt:
            damaged = bytearray(frame)
            damaged[len(damaged) // 2] ^= 0x10  # same damage as the lost-frame e2e test
            frame = bytes(damaged)
        for attempt in range(1, RETRY_LIMIT + 2):
            self.link.write(frame)
            if self.trace:
                self.trace("TX", cmd, seq, payload)
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
            if attempt <= RETRY_LIMIT:
                self.log(f"retry {attempt}/{RETRY_LIMIT} for cmd 0x{cmd:02X} ({retry_reason})")
        raise FlashError(
            f"no ACK for command 0x{cmd:02X} after {RETRY_LIMIT} retries (last: {retry_reason})"
        )

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
        fail_at_percent: int | None = None,
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
            if fail_at_percent is not None and off * 100 // total >= fail_at_percent:
                self.log(f"injecting a link fault at {off * 100 // total}% (test)")
            self.exchange(
                bp.CMD_DATA,
                off.to_bytes(4, "little") + chunk,
                corrupt=fail_at_percent is not None and off * 100 // total >= fail_at_percent,
            )
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


def reset_board() -> None:
    """Reset the MCU through the ST-Link with OpenOCD (same as pressing the RESET button)."""
    cmd = [
        "openocd",
        "-f",
        "interface/stlink.cfg",
        "-f",
        "target/stm32h7x.cfg",
        "-c",
        "init",
        "-c",
        "reset run",
        "-c",
        "exit",
    ]
    try:
        res = subprocess.run(cmd, capture_output=True, timeout=OPENOCD_TIMEOUT_S, check=False)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise FlashError(f"--reset failed to run openocd: {exc}") from exc
    if res.returncode != 0:
        raise FlashError("--reset: openocd could not reset the board (is the ST-Link attached?)")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("image", help="image file from mkimage.py")
    ap.add_argument("-p", "--port", default="auto", help="serial port or auto (ST-Link VCP)")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("--wait", type=float, default=15.0, help="seconds to wait for the bootloader")
    ap.add_argument(
        "--live", action="store_true", help="show a live colour trace of every packet (for demos)"
    )
    ap.add_argument(
        "--reset",
        action="store_true",
        help="reset the board through OpenOCD instead of pressing RESET yourself",
    )
    ap.add_argument("--no-boot", action="store_true", help="verify but do not send BOOT")
    ap.add_argument("--no-local-check", action="store_true", help="skip image validation (tests)")
    ap.add_argument(
        "--test-flip-payload-byte", type=int, metavar="N", help="corrupt payload byte N"
    )
    ap.add_argument(
        "--test-fail-at",
        type=int,
        metavar="PERCENT",
        help="damage the packet at PERCENT of the transfer until the retries run out",
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
        args.port = find_stlink_port() if args.port == "auto" else args.port
        link = serial.Serial(args.port, args.baud, timeout=0.02)
    except (OSError, im.ImageError, ImportError, FlashError) as exc:
        print(f"flash: error: {exc}", file=sys.stderr)
        return 1

    with link:
        live = livelog.LiveLog() if args.live else None
        fl = (
            Flasher(link, log=live.log, trace=live.trace, text=live.text) if live else Flasher(link)
        )
        try:
            if live:
                live.banner(f"STM32H753ZI secure bootloader — {args.port} @ {args.baud} baud")
            if args.reset:
                (live.banner if live else print)("resetting the board through the ST-Link")
                link.reset_input_buffer()
                reset_board()
            elif live:
                live.banner("waiting for bootloader: press RESET on the board")
            else:
                print(f"waiting for bootloader on {args.port} (press RESET on the board)...")
            info = fl.hello(args.wait)
            bl_version = int.from_bytes(info[4:8], "little")
            if live:
                live.banner("bootloader found, starting update")
            else:
                print(f"bootloader found: proto {info[0]}, version 0x{bl_version:08X}")
            stats = fl.update(
                image,
                boot=not args.no_boot,
                flip_payload_byte=args.test_flip_payload_byte,
                abort_after_packets=args.test_abort_after,
                fail_at_percent=args.test_fail_at,
            )
        except (FlashError, IndexError) as exc:
            if live:
                live.error(f"UPDATE FAILED: {exc}")
                live.error("application NOT booted; the bootloader stays in update mode")
            else:
                print(f"flash: FAILED: {exc}", file=sys.stderr)
            return 2
    if live:
        live.banner("UPDATE COMPLETE — application is running")
    print(
        f"done: {stats['payload_bytes']:.0f} bytes, total {stats['total_s']:.2f} s "
        f"(erase {stats['erase_s']:.2f} s, data {stats['data_s']:.2f} s, "
        f"{stats['kb_per_s']:.2f} KB/s)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
