"""End-to-end: flash.py against the real C framing/updater code (tests/sim_bootloader)."""

import os
import select
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import bl_protocol as bp
import flash
import imagefmt as im

SIM = os.environ.get("SIM_BOOTLOADER", "")
SLOT_HEADER_AREA = im.HEADER_AREA_BYTES


class SimLink:
    """pyserial-like object backed by the simulator process pipes."""

    def __init__(self, dump_path, corrupt_every=0, drop_every=0):
        self.proc = subprocess.Popen(
            [SIM, dump_path], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE
        )
        self.writes = 0
        self.corrupt_every = corrupt_every
        self.drop_every = drop_every
        self.stderr = b""

    def write(self, data):
        self.writes += 1
        if self.drop_every and self.writes % self.drop_every == 0:
            return len(data)  # simulate a lost packet
        if self.corrupt_every and self.writes % self.corrupt_every == 0:
            bad = bytearray(data)
            bad[len(bad) // 2] ^= 0x10  # damage the middle of the frame
            data = bytes(bad)
        self.proc.stdin.write(data)
        self.proc.stdin.flush()
        return len(data)

    def read(self, size=1):
        fd = self.proc.stdout.fileno()
        ready, _, _ = select.select([fd], [], [], 0.005)
        return os.read(fd, size) if ready else b""

    def reset_input_buffer(self):
        pass

    def close(self):
        self.proc.stdin.close()
        self.proc.wait(timeout=10)
        self.stderr = self.proc.stderr.read()
        self.proc.stdout.close()
        self.proc.stderr.close()


def make_image(size, version="1.0.0"):
    payload = bytes((i * 31 + 7) & 0xFF for i in range(size))
    return im.build_image(payload, im.parse_version(version))


@unittest.skipUnless(SIM and os.path.exists(SIM), "SIM_BOOTLOADER not set / built")
class FlashEndToEnd(unittest.TestCase):
    def run_flash(self, image, **link_kw):
        """Returns (result_or_exception, flash_dump_bytes, sim_stderr)."""
        with tempfile.TemporaryDirectory() as d:
            dump = os.path.join(d, "flash.bin")
            link = SimLink(dump, **link_kw)
            fl = flash.Flasher(link, log=lambda *_: None, reply_timeout=0.3, start_timeout=1.0)
            outcome = None
            try:
                fl.hello(2.0)
                update_kw = self.update_kw
                outcome = fl.update(image, **update_kw)
            except flash.FlashError as exc:
                outcome = exc
            link.close()
            with open(dump, "rb") as fh:
                mem = fh.read()
            return outcome, mem, link.stderr.decode()

    def setUp(self):
        self.update_kw = {}

    def test_full_update_writes_exact_image(self):
        image = make_image(3000)
        out, mem, err = self.run_flash(image)
        self.assertIsInstance(out, dict, out)
        self.assertEqual(mem[: len(image)], image)  # header area + payload byte for byte
        self.assertIn("boot_requested=1", err)
        self.assertGreater(out["kb_per_s"], 0)

    def test_image_not_multiple_of_flash_word(self):
        image = make_image(1000 + 5)
        out, mem, _ = self.run_flash(image)
        self.assertIsInstance(out, dict, out)
        self.assertEqual(mem[: len(image)], image)

    def test_flipped_payload_byte_rejected_at_verify(self):
        self.update_kw = {"flip_payload_byte": 500}
        out, mem, err = self.run_flash(make_image(2000))
        self.assertIsInstance(out, flash.FlashError)
        self.assertIn("VERIFY_FAIL", str(out))
        self.assertEqual(mem[:SLOT_HEADER_AREA], b"\xff" * SLOT_HEADER_AREA)  # never valid
        self.assertIn("boot_requested=0", err)

    def test_corrupt_header_rejected_at_start(self):
        image = bytearray(make_image(2000))
        image[13] ^= 0x01  # image_size field; header CRC no longer matches
        out, mem, _ = self.run_flash(bytes(image))
        self.assertIsInstance(out, flash.FlashError)
        self.assertIn("BAD_HEADER", str(out))
        self.assertEqual(mem[: SLOT_HEADER_AREA + 100], b"\xff" * (SLOT_HEADER_AREA + 100))

    def test_killed_transfer_leaves_no_valid_header(self):
        self.update_kw = {"abort_after_packets": 4}
        out, mem, err = self.run_flash(make_image(5000))
        self.assertIsInstance(out, flash.FlashError)
        self.assertEqual(mem[:SLOT_HEADER_AREA], b"\xff" * SLOT_HEADER_AREA)
        self.assertNotEqual(mem[SLOT_HEADER_AREA : SLOT_HEADER_AREA + 256], b"\xff" * 256)
        self.assertIn("boot_requested=0", err)

    def test_recovers_from_corrupted_packets(self):
        image = make_image(3000)
        out, mem, _ = self.run_flash(image, corrupt_every=5)
        self.assertIsInstance(out, dict, out)
        self.assertEqual(mem[: len(image)], image)

    def test_recovers_from_lost_packets(self):
        image = make_image(3000)
        out, mem, _ = self.run_flash(image, drop_every=7)
        self.assertIsInstance(out, dict, out)
        self.assertEqual(mem[: len(image)], image)

    def test_no_boot_option(self):
        self.update_kw = {"boot": False}
        _, _, err = self.run_flash(make_image(500))
        self.assertIn("boot_requested=0", err)
        self.assertIn("state=2", err)  # VERIFIED

    def test_hello_times_out_when_silent(self):
        class Silent:
            def write(self, data):
                return len(data)

            def read(self, size=1):
                return b""

            def reset_input_buffer(self):
                pass

        with self.assertRaises(flash.FlashError):
            flash.Flasher(Silent(), log=lambda *_: None).hello(0.3)

    def test_protocol_nack_names_cover_error_codes(self):
        self.assertEqual(set(bp.NACK_NAMES), set(range(1, 11)))


if __name__ == "__main__":
    unittest.main()
