"""
SessionLogger — auto-saves all telemetry to timestamped CSV files.

One CSV file per session (from connect → disconnect).
Also logs safety events (WARN, CRITICAL, ESTOP) with timestamps.
"""
from __future__ import annotations
import csv
import time
import logging
from pathlib import Path
from datetime import datetime
from typing import Optional, IO

from ..api.models import TelemetryFrame
from ..config.settings import LOG_DIR

logger = logging.getLogger(__name__)

CSV_HEADER = [
    "timestamp", "datetime",
    "boomAngle", "extensionMM", "measuredLoad", "actualLoad",
    "swingAngle", "ropeLength",
    "fsr1", "fsr2", "fsr3", "fsr4",
    "boomLean", "statusFlags",
    "safeLoadLimit", "loadPercent", "alarmLevel"
]


class SessionLogger:
    def __init__(self) -> None:
        self._file: Optional[IO] = None
        self._writer: Optional[csv.DictWriter] = None
        self._session_path: Optional[Path] = None
        self._active: bool = False
        self._row_count: int = 0

    def start_session(self) -> Path:
        """Open a new CSV file for this session. Returns the file path."""
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"sli_session_{timestamp}.csv"
        self._session_path = LOG_DIR / filename

        self._file = open(self._session_path, "w", newline="", encoding="utf-8")
        self._writer = csv.DictWriter(self._file, fieldnames=CSV_HEADER)
        self._writer.writeheader()
        self._active = True
        self._row_count = 0

        logger.info(f"Session log started: {self._session_path}")
        return self._session_path

    def log_frame(self, frame: TelemetryFrame) -> None:
        """Write a TelemetryFrame as a CSV row."""
        if not self._active or not self._writer:
            return

        try:
            self._writer.writerow({
                "timestamp":    frame.timestamp,
                "datetime":     datetime.fromtimestamp(frame.timestamp).isoformat(),
                "boomAngle":    round(frame.boomAngle, 3),
                "extensionMM":  round(frame.extensionMM, 2),
                "measuredLoad": round(frame.measuredLoad, 3),
                "actualLoad":   round(frame.actualLoad, 3),
                "swingAngle":   round(frame.swingAngle, 3),
                "ropeLength":   round(frame.ropeLength, 2),
                "fsr1":         frame.fsr[0],
                "fsr2":         frame.fsr[1],
                "fsr3":         frame.fsr[2],
                "fsr4":         frame.fsr[3],
                "boomLean":     round(frame.boomLean, 3),
                "statusFlags":  frame.statusFlags,
                "safeLoadLimit": round(frame.safeLoadLimit, 2),
                "loadPercent":  round(frame.loadPercent, 2),
                "alarmLevel":   frame.alarmLevel,
            })
            self._row_count += 1

            # Flush every 50 rows to avoid data loss on crash
            if self._row_count % 50 == 0:
                self._file.flush()

        except Exception as e:
            logger.error(f"CSV write error: {e}")

    def end_session(self) -> Optional[Path]:
        """Close the CSV file and return the path."""
        if not self._active:
            return None

        self._active = False
        if self._file:
            self._file.flush()
            self._file.close()
            self._file = None

        path = self._session_path
        logger.info(f"Session log closed: {path} ({self._row_count} rows)")
        return path

    def list_sessions(self) -> list[dict]:
        """Return list of all saved CSV session files with metadata."""
        sessions = []
        for f in sorted(LOG_DIR.glob("sli_session_*.csv"), reverse=True):
            stat = f.stat()
            sessions.append({
                "filename": f.name,
                "size":     stat.st_size,
                "modified": datetime.fromtimestamp(stat.st_mtime).isoformat(),
            })
        return sessions

    @property
    def is_active(self) -> bool:
        return self._active

    @property
    def current_path(self) -> Optional[Path]:
        return self._session_path
