"""Unit tests for the host tools. Run: python3 -I -m unittest discover -s tools/tests -t tools"""

import io
import struct
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import bl_protocol as bp
import flash
import imagefmt as im
import livelog
import mkimage


class CobsTests(unittest.TestCase):
    VECTORS = (
        (b"\x00", b"\x01\x01"),
        (b"\x00\x00", b"\x01\x01\x01"),
        (b"\x00\x11\x00", b"\x01\x02\x11\x01"),
        (b"\x11\x22\x00\x33", b"\x03\x11\x22\x02\x33"),
        (b"\x11\x22\x33\x44", b"\x05\x11\x22\x33\x44"),
        (b"\x11\x00\x00\x00", b"\x02\x11\x01\x01\x01"),
        (b"", b"\x01"),
        (bytes(range(1, 255)), b"\xff" + bytes(range(1, 255))),
        (bytes(range(1, 256)), b"\xff" + bytes(range(1, 255)) + b"\x02\xff"),
    )

    def test_reference_vectors(self):
        for dec, enc in self.VECTORS:
            self.assertEqual(bp.cobs_encode(dec), enc)
            self.assertEqual(bp.cobs_decode(enc), dec)

    def test_roundtrip_many(self):
        for n in (0, 1, 2, 253, 254, 255, 256, 508, 509, 600):
            for fill in (0x00, 0x01, 0x55):
                data = bytes([fill]) * n
                enc = bp.cobs_encode(data)
                self.assertNotIn(0, enc)
                self.assertEqual(bp.cobs_decode(enc), data)

    def test_decode_errors(self):
        for bad in (b"\x03\x11\x00", b"\x05\x11\x22", b"\x00"):
            with self.assertRaises(bp.ProtocolError):
                bp.cobs_decode(bad)


class PacketTests(unittest.TestCase):
    def test_golden_hello_matches_c_test(self):
        # Same bytes as tests/test_protocol.c::test_golden_hello
        self.assertEqual(
            bp.encode_packet(bp.CMD_HELLO, 0),
            bytes([0x02, 0x01, 0x01, 0x01, 0x05, 0x79, 0xB8, 0xF8, 0x99, 0x00]),
        )

    def test_roundtrip(self):
        for n in (0, 1, 28, 260):
            payload = bytes((i * 3) & 0xFF for i in range(n))
            frame = bp.encode_packet(bp.CMD_DATA, 200, payload)
            self.assertEqual(frame[-1], 0)
            self.assertEqual(bp.decode_frame(frame[:-1]), bp.Packet(bp.CMD_DATA, 200, payload))

    def test_corruption_detected(self):
        frame = bp.encode_packet(bp.CMD_START, 9, bytes(range(8)))[:-1]
        for i in range(len(frame)):
            for bit in range(8):
                bad = bytearray(frame)
                bad[i] ^= 1 << bit
                with self.assertRaises(bp.ProtocolError):
                    bp.decode_frame(bytes(bad))

    def test_too_long(self):
        with self.assertRaises(bp.ProtocolError):
            bp.encode_packet(bp.CMD_DATA, 0, bytes(261))


class ImageTests(unittest.TestCase):
    def test_crc_check_value(self):
        self.assertEqual(im.crc32(b"123456789"), 0xCBF43926)

    def test_build_and_check(self):
        payload = bytes(range(256)) * 4
        image = im.build_image(payload, im.parse_version("1.2.3"))
        self.assertEqual(len(image), im.HEADER_AREA_BYTES + len(payload))
        hdr = im.check_image(image)
        self.assertEqual(hdr.fw_version, 0x010203)
        self.assertEqual(hdr.image_size, len(payload))
        self.assertEqual(image[:4], b"SBOT")
        self.assertEqual(image[im.HEADER_BYTES : im.HEADER_AREA_BYTES], b"\xff" * (0x400 - 28))

    def test_header_layout_matches_c(self):
        # Mirrors tests/test_header.c::test_serialize_layout
        h = im.ImageHeader(im.IMAGE_MAGIC, 1, 0x00010002, 1000, 0xAABBCCDD, 0, 0)
        raw = h.to_bytes()
        self.assertEqual(raw[:4], b"SBOT")
        self.assertEqual(raw[8], 0x02)
        self.assertEqual(raw[12:14], bytes([0xE8, 0x03]))

    def test_corruption_detected(self):
        image = bytearray(im.build_image(b"\xaa" * 500, 1))
        image[im.HEADER_AREA_BYTES + 100] ^= 1
        with self.assertRaises(im.ImageError):
            im.check_image(bytes(image))
        image = bytearray(im.build_image(b"\xaa" * 500, 1))
        image[13] ^= 1  # header field
        with self.assertRaises(im.ImageError):
            im.check_image(bytes(image))

    def test_limits_and_versions(self):
        with self.assertRaises(im.ImageError):
            im.build_image(b"", 1)
        with self.assertRaises(im.ImageError):
            im.build_image(bytes(im.PAYLOAD_MAX_BYTES + 1), 1)
        self.assertEqual(im.parse_version("0x10"), 16)
        for bad in ("1.2", "1.2.256", "x"):
            with self.assertRaises(im.ImageError):
                im.parse_version(bad)


class MkimageCliTests(unittest.TestCase):
    def test_cli(self):
        with tempfile.TemporaryDirectory() as d:
            src = Path(d) / "app.bin"
            out = Path(d) / "app.img"
            src.write_bytes(b"\x01\x02\x03\x04" * 100)
            self.assertEqual(mkimage.main([str(src), "-o", str(out), "--version", "2.0.1"]), 0)
            hdr = im.check_image(out.read_bytes())
            self.assertEqual(hdr.fw_version, 0x020001)
            self.assertEqual(struct.unpack_from("<I", out.read_bytes(), 12)[0], 400)
            self.assertEqual(mkimage.main([str(Path(d) / "missing.bin"), "-o", str(out)]), 1)

    def test_pad_to(self):
        with tempfile.TemporaryDirectory() as d:
            src = Path(d) / "app.bin"
            out = Path(d) / "app.img"
            src.write_bytes(b"\x01\x02\x03\x04" * 100)
            self.assertEqual(mkimage.main([str(src), "-o", str(out), "--pad-to", "4096"]), 0)
            img = out.read_bytes()
            im.check_image(img)
            self.assertEqual(len(img), im.HEADER_AREA_BYTES + 4096)
            self.assertEqual(
                img[im.HEADER_AREA_BYTES : im.HEADER_AREA_BYTES + 400], src.read_bytes()
            )
            self.assertEqual(mkimage.main([str(src), "-o", str(out), "--pad-to", "10"]), 1)


class TextLogTests(unittest.TestCase):
    class Fake:
        def __init__(self, data):
            self.data = bytearray(data)

        def write(self, d):
            return len(d)

        def read(self, size=1):
            out = bytes(self.data[:size])
            del self.data[:size]
            return out

        def reset_input_buffer(self):
            pass

    def test_text_before_ack_is_split_off_not_fatal(self):
        ack = bp.encode_packet(bp.CMD_ACK, 7)
        link = self.Fake(b"\r\nBL: start\r\nBL: no valid app, staying\r\n" + ack)
        lines = []
        fl = flash.Flasher(link, text=lines.append)
        pkt = fl._next_packet(time.monotonic() + 1.0)
        self.assertEqual((pkt.cmd, pkt.seq), (bp.CMD_ACK, 7))
        self.assertEqual(lines, ["BL: start", "BL: no valid app, staying"])

    def test_live_log_hides_app_output_and_shows_bootloader_lines(self):
        out = io.StringIO()
        live = livelog.LiveLog(out=out, color=False)
        live.text("app slow running")
        live.text("BL: start")
        live.error("UPDATE FAILED: x")
        text = out.getvalue()
        self.assertNotIn("app slow", text)
        self.assertIn("BL: start", text)
        self.assertIn("UPDATE FAILED", text)

    def test_live_log_pacing_is_budgeted_and_accounted(self):
        naps = []
        live = livelog.LiveLog(out=io.StringIO(), color=False, pace_budget_s=1.0, sleep=naps.append)
        for seq in range(10):
            live.trace("TX", bp.CMD_HELLO, seq, b"")
        self.assertEqual(naps, [livelog.PACE_LINE_S] * 4)  # 1.0 s budget / 0.25 s per line
        self.assertAlmostEqual(live.paused_s, 1.0)

    def test_trailing_text_after_last_frame_is_reported(self):
        link = self.Fake(bp.encode_packet(bp.CMD_ACK, 1) + b"BL: booting app\r\n")
        lines = []
        fl = flash.Flasher(link, text=lines.append)
        self.assertIsNotNone(fl._next_packet(time.monotonic() + 0.3))
        self.assertIsNone(fl._next_packet(time.monotonic() + 0.1))
        self.assertEqual(lines, ["BL: booting app"])


if __name__ == "__main__":
    unittest.main()
