"""
PacketParser — parses raw ASCII lines from ESP32 into typed objects.

Handles these packet types:
  $T  → Telemetry frame (sensor data)
  $D  → Debug report lines (I2C scan results)
  $ACK → Command acknowledgements (forwarded to WebSocket)
  $LC → Load chart upload / read-back replies (forwarded to LoadChartManager)
"""
from __future__ import annotations
import time
import logging
from typing import Optional

from ..api.models import TelemetryFrame, DebugEntry, DebugReport

logger = logging.getLogger(__name__)

# $T CSV field count (must match firmware telemetry_fmt.cpp)
TELEMETRY_FIELD_COUNT = 15


class PacketParser:
    def __init__(self) -> None:
        self._debug_entries: list[DebugEntry] = []
        self._collecting_debug: bool = False
        # Callback references (set by ws_endpoint)
        self._on_telemetry = None
        self._on_debug = None
        self._on_ack = None
        self._on_loadchart = None

    def set_callbacks(
        self,
        on_telemetry=None,
        on_debug=None,
        on_ack=None,
        on_loadchart=None,
    ) -> None:
        """Register callbacks invoked when packets are fully parsed."""
        self._on_telemetry = on_telemetry
        self._on_debug = on_debug
        self._on_ack = on_ack
        self._on_loadchart = on_loadchart

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

        elif line.startswith("$LC,"):
            # Load chart replies — consumed by LoadChartManager
            if self._on_loadchart:
                self._on_loadchart(line)

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
                   ropeLenMM,fsr1,fsr2,fsr3,fsr4,boomLean,statusFlags,
                   safeLimit,loadPct,alarmLvl
        """
        try:
            parts = line.split(",")
            if len(parts) != TELEMETRY_FIELD_COUNT + 1:  # +1 for "$T" prefix
                logger.warning(
                    f"Telemetry field count mismatch: got {len(parts)-1}, "
                    f"expected {TELEMETRY_FIELD_COUNT}"
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
                boomLean      = float(parts[11]),
                statusFlags   = int(parts[12]),
                safeLoadLimit = float(parts[13]),
                loadPercent   = float(parts[14]),
                alarmLevel    = int(parts[15]),
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

        # For FSR/N20/telescope-calibration lines: parts = ["$D", "FSR1", "3412"]
        # address=None, status=raw value
        if tag.startswith("FSR") or tag == "N20_TICKS" or tag.startswith("TELE_"):
            entry = DebugEntry(bus=tag, address=None, status=address or "?")
        else:
            entry = DebugEntry(bus=tag, address=address, status=status)

        self._debug_entries.append(entry)
