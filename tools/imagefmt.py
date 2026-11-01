"""Firmware image format: header layout shared with common/image_header.h (keep in sync)."""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass

IMAGE_MAGIC = 0x544F4253  # bytes "SBOT" little endian
IMAGE_HEADER_VERSION = 1
HEADER_AREA_BYTES = 0x400  # reserved space before the payload (room for a future signature)
HEADER_BYTES = 28
HEADER_CRC_SPAN_BYTES = 24
PAYLOAD_MAX_BYTES = 0x08100000 - 0x08020400
_HEADER_FMT = "<7I"

assert struct.calcsize(_HEADER_FMT) == HEADER_BYTES


class ImageError(ValueError):
    """Raised for malformed images or headers."""


@dataclass(frozen=True)
class ImageHeader:
    magic: int
    header_version: int
    fw_version: int
    image_size: int
    image_crc32: int
    flags: int
    header_crc32: int

    def to_bytes(self) -> bytes:
        return struct.pack(
            _HEADER_FMT,
            self.magic,
            self.header_version,
            self.fw_version,
            self.image_size,
            self.image_crc32,
            self.flags,
            self.header_crc32,
        )


def crc32(data: bytes) -> int:
    """CRC-32 identical to the bootloader's (zlib / IEEE 802.3)."""
    return zlib.crc32(data) & 0xFFFFFFFF


def parse_version(text: str) -> int:
    """'1.2.3' -> 0x00010203; a plain integer (decimal or 0x..) is used as is."""
    if "." in text:
        parts = text.split(".")
        if len(parts) != 3 or not all(p.isdigit() and int(p) < 256 for p in parts):
            raise ImageError(f"version must be MAJOR.MINOR.PATCH with each part < 256: {text!r}")
        major, minor, patch = (int(p) for p in parts)
        return (major << 16) | (minor << 8) | patch
    try:
        value = int(text, 0)
    except ValueError as exc:
        raise ImageError(f"invalid version {text!r}") from exc
    if not 0 <= value <= 0xFFFFFFFF:
        raise ImageError(f"version out of range: {text!r}")
    return value


def build_header(payload: bytes, fw_version: int) -> ImageHeader:
    if not payload:
        raise ImageError("payload is empty")
    if len(payload) > PAYLOAD_MAX_BYTES:
        raise ImageError(f"payload is {len(payload)} bytes, slot holds {PAYLOAD_MAX_BYTES}")
    partial = ImageHeader(
        IMAGE_MAGIC, IMAGE_HEADER_VERSION, fw_version, len(payload), crc32(payload), 0, 0
    )
    header_crc = crc32(partial.to_bytes()[:HEADER_CRC_SPAN_BYTES])
    return ImageHeader(**{**partial.__dict__, "header_crc32": header_crc})


def pad_payload(payload: bytes, size: int) -> bytes:
    """Append deterministic filler so the payload is `size` bytes (it is never executed)."""
    if size < len(payload):
        raise ImageError(f"cannot pad to {size} bytes: the payload is already {len(payload)}")
    return payload + bytes((i * 131 + 17) & 0xFF for i in range(size - len(payload)))


def build_image(payload: bytes, fw_version: int) -> bytes:
    """Header area (header + 0xFF padding) followed by the payload."""
    header = build_header(payload, fw_version).to_bytes()
    return header + b"\xff" * (HEADER_AREA_BYTES - HEADER_BYTES) + payload


def split_image(image: bytes) -> tuple[bytes, bytes]:
    """Return (header bytes exactly as stored in the file, payload). No validation."""
    if len(image) <= HEADER_AREA_BYTES:
        raise ImageError(
            f"image file is {len(image)} bytes, "
            f"must be larger than the {HEADER_AREA_BYTES} byte header area"
        )
    return image[:HEADER_BYTES], image[HEADER_AREA_BYTES:]


def parse_header(raw: bytes) -> ImageHeader:
    if len(raw) != HEADER_BYTES:
        raise ImageError(f"header must be {HEADER_BYTES} bytes, got {len(raw)}")
    return ImageHeader(*struct.unpack(_HEADER_FMT, raw))


def check_image(image: bytes) -> ImageHeader:
    """Full local validation of an image file; raises ImageError with the reason."""
    raw, payload = split_image(image)
    hdr = parse_header(raw)
    if hdr.magic != IMAGE_MAGIC:
        raise ImageError(f"bad magic 0x{hdr.magic:08X}")
    if hdr.header_version != IMAGE_HEADER_VERSION or hdr.flags != 0:
        raise ImageError("unsupported header version or flags")
    if crc32(raw[:HEADER_CRC_SPAN_BYTES]) != hdr.header_crc32:
        raise ImageError("header CRC mismatch")
    if hdr.image_size != len(payload):
        raise ImageError(f"header says {hdr.image_size} payload bytes, file has {len(payload)}")
    if crc32(payload) != hdr.image_crc32:
        raise ImageError("payload CRC mismatch")
    return hdr
