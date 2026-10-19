#!/usr/bin/env python3
"""Hardware-in-the-loop test of the bootloader on a Nucleo-H753ZI (ST-Link VCP + OpenOCD reset).

Needs: board flashed with the bootloader, build-arm/ built, ST-Link attached to WSL.
Writes a markdown report with the measured numbers to docs/hw_test_results.md.
"""

from __future__ import annotations

import argparse
import datetime
import subprocess
import sys
import time
from pathlib import Path

import bl_protocol as bp
import flash
import imagefmt as im
import serial

ROOT = Path(__file__).resolve().parent.parent
OPENOCD = [
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
RESULTS: list[tuple[str, bool, str]] = []
MEASURED: dict[str, str] = {}


def reset_board() -> None:
    subprocess.run(OPENOCD, capture_output=True, timeout=30, check=True)


def open_link() -> serial.Serial:
    link = serial.Serial(flash.find_stlink_port(), 115200, timeout=0.02)
    link.reset_input_buffer()
    return link


def read_text(link: serial.Serial, seconds: float) -> str:
    end = time.monotonic() + seconds
    buf = b""
    while time.monotonic() < end:
        buf += link.read(200)
    return buf.decode(errors="replace")


def observe_reset(seconds: float = 2.5) -> str:
    """Reset without talking to the bootloader and return what it prints."""
    with open_link() as link:
        reset_board()
        return read_text(link, seconds)


def enter_update_mode() -> tuple[serial.Serial, flash.Flasher]:
    link = open_link()
    reset_board()
    fl = flash.Flasher(link, log=lambda *_: None)
    fl.hello(10.0)
    return link, fl


def record(name: str, ok: bool, detail: str) -> None:
    RESULTS.append((name, ok, detail))
    print(f"[{'PASS' if ok else 'FAIL'}] {name}: {detail}")


def load(name: str) -> bytes:
    return (ROOT / "build-arm" / "app_demo" / f"app_{name}.img").read_bytes()


def padded_image(size: int) -> bytes:
    code = (ROOT / "build-arm" / "app_demo" / "app_fast.bin").read_bytes()
    filler = bytes((i * 131 + 17) & 0xFF for i in range(size - len(code)))
    return im.build_image(code + filler, im.parse_version("1.0.0"))


def update(image: bytes, **kw) -> dict[str, float]:
    link, fl = enter_update_mode()
    try:
        return fl.update(image, **kw)
    finally:
        link.close()


def test_normal_update_cycle() -> None:
    for src, dst, banner in (
        ("fast", "slow", "app slow running"),
        ("slow", "fast", "app fast running"),
    ):
        stats = update(load(dst))
        with open_link() as link:  # BOOT already jumped; just listen
            text = read_text(link, 2.5)
        record(
            f"update {src} -> {dst}",
            banner in text,
            (
                f"{stats['payload_bytes']:.0f} B in {stats['total_s']:.2f} s, saw '{banner}'"
                if banner in text
                else f"banner missing, got {text!r}"
            ),
        )


def test_corrupt_payload_byte() -> None:
    try:
        update(load("slow"), flip_payload_byte=300)
        record("flipped payload byte", False, "update unexpectedly succeeded")
        return
    except flash.FlashError as exc:
        rejected = "VERIFY_FAIL" in str(exc)
    text = observe_reset()
    stays = "no valid app, staying" in text and "running" not in text
    record(
        "flipped payload byte rejected, not booted",
        rejected and stays,
        f"NACK VERIFY_FAIL={rejected}, after reset: {text.strip()!r}",
    )
    update(load("fast"))  # restore


def test_bad_headers_do_not_erase() -> None:
    good = load("fast")
    payload = good[im.HEADER_AREA_BYTES :]
    base = im.build_header(payload, 1)

    def with_header(**changes) -> bytes:
        fields = {**base.__dict__, **changes}
        fields["header_crc32"] = 0
        h = im.ImageHeader(**fields)
        crc = im.crc32(h.to_bytes()[: im.HEADER_CRC_SPAN_BYTES])
        h = im.ImageHeader(**{**fields, "header_crc32": crc})
        return h.to_bytes() + b"\xff" * (im.HEADER_AREA_BYTES - im.HEADER_BYTES) + payload

    bad_crc = bytearray(good)
    bad_crc[24] ^= 0x01
    cases = {
        "wrong magic": with_header(magic=0x12345678),
        "oversized image": with_header(image_size=im.PAYLOAD_MAX_BYTES + 1),
        "bad header CRC": bytes(bad_crc),
    }
    for name, image in cases.items():
        link, fl = enter_update_mode()
        try:
            fl.update(image)
            rejected = False
        except flash.FlashError as exc:
            rejected = "BAD_HEADER" in str(exc)
        link.close()
        text = observe_reset()  # the old app must still be there: START never erased it
        still = "app fast running" in text
        record(
            f"{name} rejected at START, old app intact",
            rejected and still,
            f"NACK BAD_HEADER={rejected}, old app boots={still}",
        )


def test_killed_transfer() -> None:
    try:
        update(padded_image(4096), abort_after_packets=6)
    except flash.FlashError:
        pass
    text = observe_reset()
    record(
        "transfer killed halfway, then reset: not booted",
        "no valid app, staying" in text and "running" not in text,
        f"after reset: {text.strip()!r}",
    )
    update(load("fast"))


def raw(fl: flash.Flasher, cmd: int, seq: int, payload: bytes = b"", wait: float = 1.0):
    fl.link.write(bp.encode_packet(cmd, seq, payload))
    end = time.monotonic() + wait
    while True:
        pkt = fl._next_packet(end)
        if pkt is None or pkt.seq == seq:
            return pkt


def test_out_of_order_and_duplicates() -> None:
    image = load("fast")
    header, payload = im.split_image(image)
    link, fl = enter_update_mode()
    ok = True
    notes = []
    fl.exchange(bp.CMD_START, header, timeout=fl.start_timeout)

    def data(off: int) -> bytes:
        return off.to_bytes(4, "little") + payload[off : off + 256]

    s = fl.seq
    fl.exchange(bp.CMD_DATA, data(0))
    dup = raw(fl, bp.CMD_DATA, s, data(0))  # same seq again: lost-ACK case
    good = dup is not None and dup.cmd == bp.CMD_ACK
    notes.append(f"duplicate DATA -> {'ACK' if good else dup}")
    ok &= good
    skip = raw(fl, bp.CMD_DATA, fl.seq, data(512))  # offset skipped ahead
    good = skip is not None and skip.cmd == bp.CMD_NACK and skip.payload == bytes([4])
    notes.append(f"out-of-order offset -> {'NACK BAD_OFFSET' if good else skip}")
    ok &= good
    bad_seq = raw(fl, bp.CMD_DATA, (fl.seq + 5) & 0xFF, data(256))
    good = bad_seq is not None and bad_seq.cmd == bp.CMD_NACK and bad_seq.payload == bytes([3])
    notes.append(f"wrong seq -> {'NACK BAD_SEQ' if good else bad_seq}")
    ok &= good
    try:  # carry on normally from where we are; success proves nothing was written twice
        for off in range(256, len(payload), 256):
            fl.exchange(bp.CMD_DATA, data(off))
        fl.exchange(bp.CMD_VERIFY, timeout=5.0)
        fl.exchange(bp.CMD_BOOT)
        notes.append("completed update after the errors, VERIFY ok")
    except flash.FlashError as exc:
        ok = False
        notes.append(f"could not finish: {exc}")
    link.close()
    with open_link() as lk:
        text = read_text(lk, 2.5)
    ok &= "app fast running" in text
    record("out-of-order and duplicate packets handled", ok, "; ".join(notes))


def test_throughput() -> None:
    for size in (32 * 1024, 128 * 1024):
        stats = update(padded_image(size))
        MEASURED[f"{size // 1024} KB"] = (
            f"total {stats['total_s']:.2f} s, erase {stats['erase_s']:.2f} s, "
            f"data {stats['data_s']:.2f} s, {stats['kb_per_s']:.2f} KB/s"
        )
        record(f"throughput {size // 1024} KB image", True, MEASURED[f"{size // 1024} KB"])
    update(load("fast"))  # restore


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--report", type=Path, default=ROOT / "docs" / "hw_test_results.md")
    args = ap.parse_args()
    try:
        for t in (
            test_normal_update_cycle,
            test_corrupt_payload_byte,
            test_bad_headers_do_not_erase,
            test_killed_transfer,
            test_out_of_order_and_duplicates,
            test_throughput,
        ):
            print(f"--- {t.__name__}")
            t()
    except (flash.FlashError, subprocess.SubprocessError, OSError) as exc:
        record("test run aborted", False, str(exc))
    lines = [
        "# Hardware test results",
        "",
        f"Run: {datetime.datetime.now().isoformat(timespec='seconds')} on Nucleo-H753ZI, "
        "USART3 115200 8N1 via ST-Link VCP, `tools/hwtest.py`.",
        "",
        "| Test | Result | Detail |",
        "|---|---|---|",
    ]
    for name, ok, detail in RESULTS:
        lines.append(f"| {name} | {'PASS' if ok else 'FAIL'} | {detail.replace('|', '/')} |")
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text("\n".join(lines) + "\n")
    failed = [r for r in RESULTS if not r[1]]
    print(f"\n{len(RESULTS) - len(failed)}/{len(RESULTS)} passed; report: {args.report}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
