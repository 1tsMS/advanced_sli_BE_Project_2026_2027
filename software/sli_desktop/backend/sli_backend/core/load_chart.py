"""
LoadChartManager — uploads and reads back the rated-capacity chart stored in
the ESP32's flash, waiting for the firmware's replies instead of assuming
success.

Protocol (see firmware load_chart.h):
    LC UPLOAD <n>    -> $LC,READY,<n>   (or $LC,ERR,<why>)
    LC a,e,l  (x n)  -> silent unless it fails: $LC,ERR,<why>
    LC SAVE          -> $LC,OK,<n>      (or $LC,ERR,<why>)
    LC GET           -> $LC,BEGIN,<n> / $LC,E,<i>,a,e,l ... / $LC,END
"""
from __future__ import annotations
import asyncio
import logging
from typing import List, Optional, Tuple

from .command_router import CommandRouter
from ..api.models import LoadChartEntry

logger = logging.getLogger(__name__)

# The ESP32 writes flash on LC SAVE, which can take a moment
_REPLY_TIMEOUT_S = 2.0
_SAVE_TIMEOUT_S = 4.0
# Small gap between entries so the ESP32's serial buffer is never overrun
_ENTRY_GAP_S = 0.02

# Events pushed by handle_line: ("status", "$LC,OK,5") or ("chart", [entries])
_Event = Tuple[str, object]


class LoadChartError(Exception):
    """The ESP32 rejected the request, or did not answer in time."""


class LoadChartManager:
    def __init__(self, router: CommandRouter) -> None:
        self._router = router
        self._events: asyncio.Queue[_Event] = asyncio.Queue()
        self._dump: Optional[List[LoadChartEntry]] = None   # read-back in progress
        self._lock = asyncio.Lock()                          # one request at a time

    # ------------------------------------------------------------------ #
    # Called by PacketParser for every "$LC," line (inside the event loop)
    # ------------------------------------------------------------------ #

    def handle_line(self, line: str) -> None:
        parts = line.split(",")
        if len(parts) < 2:
            return
        tag = parts[1]

        if tag == "BEGIN":
            self._dump = []
        elif tag == "E" and self._dump is not None and len(parts) >= 6:
            try:
                self._dump.append(LoadChartEntry(
                    angle=float(parts[3]),
                    extensionMM=float(parts[4]),
                    limitKg=float(parts[5]),
                ))
            except ValueError:
                logger.warning(f"Bad load chart entry from ESP32: {line}")
        elif tag == "END" and self._dump is not None:
            self._events.put_nowait(("chart", self._dump))
            self._dump = None
        elif tag in ("READY", "OK", "ERR"):
            self._events.put_nowait(("status", line))

    # ------------------------------------------------------------------ #
    # Public API
    # ------------------------------------------------------------------ #

    async def upload(self, entries: List[LoadChartEntry]) -> int:
        """Upload and save a chart. Returns the entry count the ESP32 confirmed."""
        async with self._lock:
            self._drain()

            self._send(f"LC UPLOAD {len(entries)}")
            await self._wait_status("READY", _REPLY_TIMEOUT_S)

            for entry in entries:
                self._send(f"LC {entry.angle:.3f},{entry.extensionMM:.3f},{entry.limitKg:.3f}")
                await asyncio.sleep(_ENTRY_GAP_S)
                self._raise_if_rejected()   # e.g. $LC,ERR,RANGE for this entry

            self._send("LC SAVE")
            reply = await self._wait_status("OK", _SAVE_TIMEOUT_S)
            confirmed = int(reply.split(",")[2])
            if confirmed != len(entries):
                raise LoadChartError(
                    f"ESP32 saved {confirmed} entries, expected {len(entries)}"
                )
            return confirmed

    async def read(self) -> List[LoadChartEntry]:
        """Read the chart currently stored on the ESP32."""
        async with self._lock:
            self._drain()
            self._dump = None

            self._send("LC GET")
            try:
                while True:
                    kind, payload = await asyncio.wait_for(
                        self._events.get(), _REPLY_TIMEOUT_S
                    )
                    if kind == "chart":
                        return payload  # type: ignore[return-value]
                    self._raise_if_error_line(str(payload))
            except asyncio.TimeoutError:
                raise LoadChartError("No reply from ESP32 to LC GET")
            finally:
                self._dump = None

    # ------------------------------------------------------------------ #
    # Internals
    # ------------------------------------------------------------------ #

    def _send(self, command: str) -> None:
        ok, msg = self._router.send(command)
        if not ok:
            raise LoadChartError(msg)

    def _drain(self) -> None:
        while not self._events.empty():
            self._events.get_nowait()

    @staticmethod
    def _raise_if_error_line(line: str) -> None:
        if line.startswith("$LC,ERR"):
            raise LoadChartError(f"ESP32 rejected the request: {line.split(',')[-1]}")

    def _raise_if_rejected(self) -> None:
        """Raise if an ERR reply has arrived (used between entries)."""
        while not self._events.empty():
            kind, payload = self._events.get_nowait()
            if kind == "status":
                self._raise_if_error_line(str(payload))

    async def _wait_status(self, expected: str, timeout: float) -> str:
        """Wait for a `$LC,<expected>,...` line; an ERR line raises."""
        try:
            while True:
                kind, payload = await asyncio.wait_for(self._events.get(), timeout)
                if kind != "status":
                    continue
                line = str(payload)
                self._raise_if_error_line(line)
                if line.split(",")[1] == expected:
                    return line
        except asyncio.TimeoutError:
            raise LoadChartError(
                f"No '{expected}' reply from ESP32 "
                "(is it connected, and flashed with the load chart handler?)"
            )
