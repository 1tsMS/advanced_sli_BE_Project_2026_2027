"""
PacketParser — parses raw ASCII lines from ESP32 into typed objects.

Handles two packet types:
  $T  → Telemetry frame (sensor data)
  $D  → Debug report lines (I2C scan results)
  $ACK → Command acknowledgements (forwarded to WebSocket)
"""
from __future__ import annotations
import time
import logging
from typing import Optional

from ..api.models import TelemetryFrame, DebugEntry, DebugReport, SafetyStatus
from .load_chart import ChartReadback

logger = logging.getLogger(__name__)

# $T CSV field count (must match firmware telemetry_fmt.cpp)
TELEMETRY_FIELD_COUNT = 15
TELEMETRY_FIELD_COUNT_ROPE = 17   # + ropeTotalMM, hookDropMM (firmware from 2026-10-29)


class PacketParser:
    def __init__(self) -> None:
        self._debug_entries: list[DebugEntry] = []
        self._collecting_debug: bool = False
        # Callback references (set by ws_endpoint)
        self._on_telemetry = None
        self._on_debug = None
        self._on_ack = None
        self._on_safety = None
        self._on_chart = None
        self._on_shaping = None
        self._chart = ChartReadback()

    def set_callbacks(
        self,
        on_telemetry=None,
        on_debug=None,
        on_ack=None,
        on_safety=None,
        on_chart=None,
        on_shaping=None,
    ) -> None:
        """Register callbacks invoked when packets are fully parsed."""
        self._on_telemetry = on_telemetry
        self._on_debug = on_debug
        self._on_ack = on_ack
        self._on_safety = on_safety
        self._on_chart = on_chart
        self._on_shaping = on_shaping

    @staticmethod
    def parse_shaping(line: str) -> Optional[dict]:
        """$SHP,<0|1>,<pendulum length mm>,<T/2 ms>"""
        parts = line.strip().split(",")
        if len(parts) != 4:
            return None
        try:
            return {"on": parts[1] == "1", "lengthMm": float(parts[2]), "halfPeriodMs": int(parts[3])}
        except ValueError:
            return None

    @staticmethod
    def parse_safety(line: str) -> Optional[SafetyStatus]:
        """$S,state,pct,capKg,zone,bypassLeftS,blockedHex,chartName[,mode[,why]] (chart names
        never contain commas; firmware before SLI OFF sends no mode: then it is ARMED or BYPASS;
        why = reason for a FAULT / out-of-chart cut, from 2026-11-04 firmware)."""
        parts = line.strip().split(",")
        if len(parts) not in (8, 9, 10):
            return None
        try:
            left = int(parts[5])
            mode = parts[8] if len(parts) >= 9 else ("BYPASS" if left > 0 else "ARMED")
            why = parts[9] if len(parts) == 10 else "-"
            return SafetyStatus(state=parts[1], pct=float(parts[2]), capKg=float(parts[3]),
                                zone=int(parts[4]), bypassLeftS=left,
                                blocked=int(parts[6], 16), chart=parts[7], mode=mode, why=why,
                                timestamp=time.time())
        except ValueError:
            return None

    def parse_line(self, line: str) -> None:
        """
        Parse a single raw ASCII line from ESP32.
        Routes to the appropriate handler based on prefix.
        """
        if not line:
            return

        if line.startswith("$T,"):
            frame = self._parse_telemetry(line)
            if frame and self._on_telemetry:
                self._on_telemetry(frame)

        elif line.startswith("$D,"):
            self._handle_debug_line(line)

        elif line.startswith("$ACK,"):
            if self._on_ack:
                self._on_ack(line)

        elif line.startswith("$S,"):
            status = self.parse_safety(line)
            if status and self._on_safety:
                self._on_safety(status)

        elif line.startswith("$SHP,"):
            shp = self.parse_shaping(line)
            if shp and self._on_shaping:
                self._on_shaping(shp)

        elif line.startswith("$LC,"):
            chart = self._chart.feed(line)
            if chart is not None and self._on_chart:
                self._on_chart(chart)

        else:
            # Unrecognized line — log for visibility but don't crash
            logger.debug(f"Unhandled ESP32 line: {line}")

    # ------------------------------------------------------------------ #
    # Telemetry parser
    # ------------------------------------------------------------------ #

    def _parse_telemetry(self, line: str) -> Optional[TelemetryFrame]:
        """
        Parse $T CSV line into TelemetryFrame.
        Format: $T,boomAngle,extensionMM,measuredLoad,actualLoad,swingAngle,
                   ropeLenMM,fsr1,fsr2,fsr3,fsr4,imuRoll,imuPitch,
                   safeLimit,loadPct,alarmLvl[,ropeTotalMM,hookDropMM]
        The last two were appended later; older firmware sends 15 fields and
        they are then left at 0.
        """
        try:
            parts = line.split(",")
            n = len(parts) - 1                             # minus the "$T" prefix
            if n not in (TELEMETRY_FIELD_COUNT, TELEMETRY_FIELD_COUNT_ROPE):
                logger.warning(
                    f"Telemetry field count mismatch: got {n}, "
                    f"expected {TELEMETRY_FIELD_COUNT} or {TELEMETRY_FIELD_COUNT_ROPE}"
                )
                return None

            frame = TelemetryFrame(
                boomAngle     = float(parts[1]),
                extensionMM   = float(parts[2]),
                measuredLoad  = float(parts[3]),
                actualLoad    = float(parts[4]),
                swingAngle    = float(parts[5]),
                ropeLength    = float(parts[6]),
                fsr           = [int(parts[7]), int(parts[8]),
                                 int(parts[9]), int(parts[10])],
                imuRoll       = float(parts[11]),
                imuPitch      = float(parts[12]),
                safeLoadLimit = float(parts[13]),
                loadPercent   = float(parts[14]),
                alarmLevel    = int(parts[15]),
                ropeTotal     = float(parts[16]) if n >= 17 else 0.0,
                hookDrop      = float(parts[17]) if n >= 17 else 0.0,
                timestamp     = time.time(),
            )
            return frame

        except (ValueError, IndexError) as e:
            logger.warning(f"Failed to parse telemetry line: {e} | line='{line}'")
            return None

    # ------------------------------------------------------------------ #
    # Debug report handler
    # ------------------------------------------------------------------ #

    def _handle_debug_line(self, line: str) -> None:
        """
        Accumulate $D lines until $D,END is received, then fire callback.
        """
        parts = line.split(",")
        if len(parts) < 2:
            return

        tag = parts[1]  # e.g. "I2C0", "HX711", "FSR1", "END"

        if tag == "END":
            # Report complete — fire callback and clear buffer
            report = DebugReport(entries=self._debug_entries)
            if self._on_debug:
                self._on_debug(report)
            self._debug_entries = []
            return

        # Build DebugEntry
        address = parts[2] if len(parts) > 2 else None
        status  = parts[3] if len(parts) > 3 else (parts[2] if len(parts) > 2 else "?")

        # For FSR/N20 lines: parts = ["$D", "FSR1", "3412"]
        # address=None, status=raw value
        if tag.startswith("FSR") or tag == "N20_TICKS":
            entry = DebugEntry(bus=tag, address=None, status=address or "?")
        else:
            entry = DebugEntry(bus=tag, address=address, status=status)

        self._debug_entries.append(entry)
