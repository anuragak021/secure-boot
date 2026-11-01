#!/usr/bin/env python3
"""Wrap a raw application .bin into a bootloader image (0x400 byte header area + payload)."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from imagefmt import HEADER_AREA_BYTES, ImageError, build_image, check_image, parse_version


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("input", type=Path, help="raw application binary (linked at 0x08020400)")
    ap.add_argument("-o", "--output", type=Path, required=True, help="output image file")
    ap.add_argument(
        "--version", default="1.0.0", help="MAJOR.MINOR.PATCH or integer (default 1.0.0)"
    )
    ap.add_argument(
        "--pad-to",
        type=int,
        metavar="BYTES",
        help="append filler after the app so the payload is BYTES long (demo of a big transfer)",
    )
    args = ap.parse_args(argv)

    try:
        payload = args.input.read_bytes()
        if args.pad_to is not None:
            if args.pad_to < len(payload):
                raise ImageError(
                    f"--pad-to {args.pad_to} is smaller than the app ({len(payload)} B)"
                )
            payload += bytes((i * 131 + 17) & 0xFF for i in range(args.pad_to - len(payload)))
        image = build_image(payload, parse_version(args.version))
        hdr = check_image(image)
    except (OSError, ImageError) as exc:
        print(f"mkimage: error: {exc}", file=sys.stderr)
        return 1

    args.output.write_bytes(image)
    print(
        f"mkimage: {args.output} header_area={HEADER_AREA_BYTES} payload={hdr.image_size} "
        f"crc32=0x{hdr.image_crc32:08X} version=0x{hdr.fw_version:08X}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
