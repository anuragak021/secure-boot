#!/usr/bin/env python3
"""Screen-recording demo: one rejected update, then one good update, with the live trace.

Runs flash.py twice on the real board (it resets the board itself through OpenOCD):
  1. a link fault is injected near the end of the transfer; the bootloader rejects the image
     and the app is not booted;
  2. the same image is flashed cleanly and the app boots.
The first seconds of each trace are slowed down (see flash.py --live-pace); everything after
that runs at the real speed of the transfer.
"""

from __future__ import annotations

import argparse
import sys
import tempfile
import time
from pathlib import Path

import flash
import imagefmt as im

DEFAULT_IMAGE = Path(__file__).resolve().parent.parent / "build-arm" / "app_demo" / "app_slow.img"
# The demo image is padded to 2326 bytes = 10 DATA packets, so the 8th packet (starting at 77%)
# ends at 88.0% of the transfer, where the progress bar is when the fault hits.
DEMO_PAYLOAD_BYTES = 2326
DEFAULT_FAIL_AT_PERCENT = 77
CLEAR_SCREEN = "\x1b[2J\x1b[H"


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("image", nargs="?", type=Path, default=DEFAULT_IMAGE, help="image to flash")
    ap.add_argument("--fail-at", type=int, default=DEFAULT_FAIL_AT_PERCENT, metavar="PERCENT")
    ap.add_argument("--live-pace", type=float, default=flash.DEFAULT_LIVE_PACE_S, metavar="SECONDS")
    ap.add_argument("--pause", type=float, default=3.0, help="seconds between the two runs")
    args = ap.parse_args(argv)

    try:
        header, payload = im.split_image(args.image.read_bytes())
        fw_version = im.parse_header(header).fw_version
        padded = im.build_image(im.pad_payload(payload, DEMO_PAYLOAD_BYTES), fw_version)
    except (OSError, im.ImageError) as exc:
        print(f"demo: error: {exc}", file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory() as tmp:
        image_path = Path(tmp) / "demo.img"
        image_path.write_bytes(padded)
        return run_demo(args, image_path)


def run_demo(args: argparse.Namespace, image: Path) -> int:
    common = ["--live", "--reset", "--live-pace", str(args.live_pace)]
    runs = [
        ("rejected update", [*common, "--test-fail-at", str(args.fail_at), str(image)], 2),
        ("good update", [*common, str(image)], 0),
    ]
    for index, (name, flash_args, expected_rc) in enumerate(runs):
        if index:
            time.sleep(args.pause)
        print(CLEAR_SCREEN, end="")
        print(f"$ python3 tools/flash.py {' '.join(flash_args)}", flush=True)
        rc = flash.main(flash_args)
        if rc != expected_rc:
            print(f"demo: the {name} ended with code {rc}, expected {expected_rc}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
