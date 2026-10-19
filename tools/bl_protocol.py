"""Host side of the bootloader wire protocol (COBS, packets); mirrors protocol_codec.c."""

from __future__ import annotations

import struct
from dataclasses import dataclass

from imagefmt import crc32

CMD_HELLO = 0x01
CMD_START = 0x02
CMD_DATA = 0x03
CMD_VERIFY = 0x04
CMD_BOOT = 0x05
CMD_ACK = 0x80
CMD_NACK = 0x81

MAX_DATA_BYTES = 256
MAX_PAYLOAD_BYTES = 4 + MAX_DATA_BYTES

NACK_NAMES = {
    1: "BAD_CRC",
    2: "BAD_LEN",
    3: "BAD_SEQ",
    4: "BAD_OFFSET",
    5: "BAD_HEADER",
    6: "FLASH_ERASE",
    7: "FLASH_WRITE",
    8: "VERIFY_FAIL",
    9: "NOT_READY",
    10: "BAD_CMD",
}


class ProtocolError(ValueError):
    """Malformed frame or packet."""


def cobs_encode(data: bytes) -> bytes:
    """Minimal (reference) COBS encoding, no delimiter."""
    out = bytearray([0])
    code_pos = 0
    code = 1
    closed_full = False
    for b in data:
        closed_full = False
        if b == 0:
            out[code_pos] = code
            code_pos = len(out)
            out.append(0)
            code = 1
        else:
            out.append(b)
            code += 1
            if code == 0xFF:
                out[code_pos] = code
                code_pos = len(out)
                out.append(0)
                code = 1
                closed_full = True
    if closed_full:
        out.pop()
    else:
        out[code_pos] = code
    return bytes(out)


def cobs_decode(data: bytes) -> bytes:
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        code = data[i]
        i += 1
        if code == 0:
            raise ProtocolError("zero byte inside COBS data")
        if code - 1 > n - i:
            raise ProtocolError("truncated COBS block")
        block = data[i : i + code - 1]
        if 0 in block:
            raise ProtocolError("zero byte inside COBS block")
        out += block
        i += code - 1
        if code < 0xFF and i < n:
            out.append(0)
    return bytes(out)


@dataclass(frozen=True)
class Packet:
    cmd: int
    seq: int
    payload: bytes


def encode_packet(cmd: int, seq: int, payload: bytes = b"") -> bytes:
    """Wire frame: COBS(cmd | seq | len16 | payload | crc32) + 0x00."""
    if len(payload) > MAX_PAYLOAD_BYTES:
        raise ProtocolError(f"payload of {len(payload)} bytes exceeds {MAX_PAYLOAD_BYTES}")
    body = struct.pack("<BBH", cmd, seq, len(payload)) + payload
    return cobs_encode(body + struct.pack("<I", crc32(body))) + b"\x00"


def decode_frame(frame: bytes) -> Packet:
    """Decode one frame without its trailing delimiter."""
    raw = cobs_decode(frame)
    if len(raw) < 8:
        raise ProtocolError("packet too short")
    cmd, seq, length = struct.unpack_from("<BBH", raw)
    if length > MAX_PAYLOAD_BYTES or len(raw) != 8 + length:
        raise ProtocolError("length field does not match packet size")
    (crc,) = struct.unpack_from("<I", raw, 4 + length)
    if crc != crc32(raw[: 4 + length]):
        raise ProtocolError("packet CRC mismatch")
    return Packet(cmd, seq, raw[4 : 4 + length])
