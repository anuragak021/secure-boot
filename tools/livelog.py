"""Live, human-readable trace of the bootloader conversation (for demos and screen recordings).

Plugs into flash.Flasher as its `log` and `trace` callbacks. Pure host side: it only decodes
packets the flasher already sends and receives, so it shows exactly what is on the wire.
"""

from __future__ import annotations

import os
import sys
import time
from typing import TextIO

import bl_protocol as bp
import imagefmt as im

APP_SLOT_BASE = 0x0802_0400  # first byte after the 0x400 header area
BAR_WIDTH = 24
PACE_LINE_S = 0.25  # demo pacing: pause after each packet line so the arrows are readable
BOOTLOADER_LOG_PREFIX = "BL:"

CMD_NAMES = {
    bp.CMD_HELLO: "HELLO",
    bp.CMD_START: "START",
    bp.CMD_DATA: "DATA",
    bp.CMD_VERIFY: "VERIFY",
    bp.CMD_BOOT: "BOOT",
    bp.CMD_ACK: "ACK",
    bp.CMD_NACK: "NACK",
}

_RESET = "\x1b[0m"
_DIM = "\x1b[2m"
_BOLD = "\x1b[1m"
_RED = "\x1b[31m"
_GREEN = "\x1b[32m"
_YELLOW = "\x1b[33m"
_CYAN = "\x1b[36m"
_MAGENTA = "\x1b[35m"


class LiveLog:
    """Prints one coloured, timestamped line per packet; usable as Flasher(log=, trace=)."""

    def __init__(
        self,
        out: TextIO | None = None,
        color: bool | None = None,
        pace_budget_s: float = 0.0,
        sleep=time.sleep,
    ) -> None:
        self.out = out if out is not None else sys.stdout
        if color is None:
            color = self.out.isatty() and "NO_COLOR" not in os.environ
        self.color = color
        self._t0 = time.monotonic()
        self._total = 0
        self._sent = 0
        self._sleep = sleep
        self._pace_left_s = pace_budget_s
        self.paused_s = 0.0  # total demo pacing added to the run

    # --- formatting helpers ---------------------------------------------------------------
    def _c(self, style: str, text: str) -> str:
        return f"{style}{text}{_RESET}" if self.color else text

    def _stamp(self) -> str:
        return self._c(_DIM, f"[{time.monotonic() - self._t0:8.3f}s]")

    def _emit(self, line: str) -> None:
        print(f"{self._stamp()} {line}", file=self.out, flush=True)

    def _bar(self) -> str:
        if self._total == 0:
            return ""
        frac = min(self._sent / self._total, 1.0)
        filled = int(frac * BAR_WIDTH)
        bar = "█" * filled + "░" * (BAR_WIDTH - filled)
        return f" {self._c(_CYAN, bar)} {frac * 100:5.1f}%"

    # --- Flasher callbacks ----------------------------------------------------------------
    def log(self, message: str) -> None:
        """Flasher.log replacement: status lines (retries, phase changes)."""
        self._emit(self._c(_YELLOW, f"•  {message.strip()}"))

    def text(self, line: str) -> None:
        """Flasher.text: a log line the bootloader itself printed on the UART."""
        if not line.startswith(BOOTLOADER_LOG_PREFIX):
            return  # the demo app's own output is not part of the update story
        self._emit(f"{self._c(_MAGENTA, 'MCU log      ')} {self._c(_BOLD, line)}")

    def error(self, text: str) -> None:
        """A red banner for a failed update."""
        self._emit(self._c(_BOLD + _RED, f"✖✖ {text} ✖✖"))

    def banner(self, text: str) -> None:
        """A highlighted line for the caller's own milestones."""
        self._emit(self._c(_BOLD + _MAGENTA, f"══ {text} ══"))

    def trace(self, direction: str, cmd: int, seq: int, payload: bytes) -> None:
        """Flasher.trace: called for every packet sent ("TX") or received ("RX")."""
        name = CMD_NAMES.get(cmd, f"0x{cmd:02X}")
        arrow = (
            self._c(_GREEN, "PC  ──▶ MCU") if direction == "TX" else self._c(_CYAN, "PC  ◀── MCU")
        )
        head = f"{arrow}  {self._c(_BOLD, f'{name:<6}')} seq={seq:3d} "
        self._emit(head + self._describe(direction, cmd, payload))
        self._pace()

    def _pace(self) -> None:
        """Slow the first packet lines down (budgeted). paused_s says whether this happened."""
        if self._pace_left_s < PACE_LINE_S:
            return
        self._sleep(PACE_LINE_S)
        self._pace_left_s -= PACE_LINE_S
        self.paused_s += PACE_LINE_S

    def _describe(self, direction: str, cmd: int, payload: bytes) -> str:
        if cmd == bp.CMD_HELLO:
            return "handshake"
        if cmd == bp.CMD_ACK:
            if len(payload) >= 8:  # HELLO reply: proto, slot info, bootloader version
                version = int.from_bytes(payload[4:8], "little")
                return self._c(_GREEN, f"OK  (bootloader v0x{version:08X}, proto {payload[0]})")
            return self._c(_GREEN, "OK")
        if cmd == bp.CMD_NACK:
            code = payload[0] if payload else 0
            return self._c(_BOLD + _RED, f"REJECTED: {bp.NACK_NAMES.get(code, f'code {code}')}")
        if cmd == bp.CMD_START and direction == "TX":
            return self._describe_start(payload)
        if cmd == bp.CMD_DATA and direction == "TX":
            return self._describe_data(payload)
        if cmd == bp.CMD_VERIFY:
            return "full-image CRC32 check, header is written to flash only if it passes"
        if cmd == bp.CMD_BOOT:
            return "jump to the verified application"
        return ""

    def _describe_start(self, header: bytes) -> str:
        try:
            hdr = im.parse_header(header)
        except (im.ImageError, ValueError, TypeError):
            return f"{len(header)}-byte header"
        self._total = hdr.image_size
        self._sent = 0
        return (
            f"image {hdr.image_size} B, fw v0x{hdr.fw_version:08X}, "
            f"crc32 0x{hdr.image_crc32:08X} — erasing app slot"
        )

    def _describe_data(self, payload: bytes) -> str:
        offset = int.from_bytes(payload[:4], "little")
        size = len(payload) - 4
        self._sent = max(self._sent, offset + size)
        return f"flash 0x{APP_SLOT_BASE + offset:08X} +{size:3d} B{self._bar()}"
