"""
SessionLogger — records telemetry to a timestamped CSV file, on demand.

Recording is OFF by default: a file is only written between an explicit
start (dashboard REC button -> POST /api/recording/start) and stop
(REC button, disconnect, or the serial link dropping).
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
    "imuRoll", "imuPitch",
    "safeLoadLimit", "loadPercent", "alarmLevel",
    "ropeTotal", "hookDrop",
]


class SessionLogger:
    def __init__(self, log_dir: Path = LOG_DIR) -> None:
        self._log_dir = log_dir
        self._file: Optional[IO] = None
        self._writer: Optional[csv.DictWriter] = None
        self._session_path: Optional[Path] = None
        self._active: bool = False
        self._row_count: int = 0
        self._started_at: Optional[float] = None

    def start_session(self) -> Path:
        """Open a new CSV file. If one is already open it is kept (idempotent)."""
        if self._active and self._session_path:
            return self._session_path
        self._log_dir.mkdir(parents=True, exist_ok=True)
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        self._session_path = self._log_dir / f"sli_session_{timestamp}.csv"
        self._file = open(self._session_path, "w", newline="", encoding="utf-8")
        self._writer = csv.DictWriter(self._file, fieldnames=CSV_HEADER)
        self._writer.writeheader()
        self._active = True
        self._row_count = 0
        self._started_at = time.time()
        logger.info(f"Recording started: {self._session_path}")
        return self._session_path

    def log_frame(self, frame: TelemetryFrame) -> None:
        """Write a TelemetryFrame as a CSV row (no-op unless recording)."""
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
                "imuRoll":      round(frame.imuRoll, 3),
                "imuPitch":     round(frame.imuPitch, 3),
                "safeLoadLimit": round(frame.safeLoadLimit, 2),
                "loadPercent":  round(frame.loadPercent, 2),
                "alarmLevel":   frame.alarmLevel,
                "ropeTotal":    round(frame.ropeTotal, 1),
                "hookDrop":     round(frame.hookDrop, 1),
            })
            self._row_count += 1
            # Flush every 50 rows to avoid data loss on crash
            if self._row_count % 50 == 0:
                self._file.flush()
        except Exception as e:
            logger.error(f"CSV write error: {e}")

    def end_session(self) -> Optional[Path]:
        """Close the CSV file and return its path (None if not recording)."""
        if not self._active:
            return None
        self._active = False
        if self._file:
            self._file.flush()
            self._file.close()
            self._file = None
        self._writer = None
        path = self._session_path
        logger.info(f"Recording stopped: {path} ({self._row_count} rows)")
        return path

    def state(self) -> dict:
        """Recording state for the API / dashboard."""
        return {
            "recording":   self._active,
            "file":        self._session_path.name if (self._active and self._session_path) else None,
            "rows":        self._row_count if self._active else 0,
            "elapsedSec":  round(time.time() - self._started_at, 1) if (self._active and self._started_at) else 0.0,
        }

    def list_sessions(self) -> list[dict]:
        """Return all saved CSV session files with metadata (newest first)."""
        sessions = []
        for f in sorted(self._log_dir.glob("sli_session_*.csv"), reverse=True):
            stat = f.stat()
            sessions.append({
                "filename": f.name,
                "size":     stat.st_size,
                "modified": datetime.fromtimestamp(stat.st_mtime).isoformat(),
            })
        return sessions

    def resolve_log(self, filename: str) -> Optional[Path]:
        """Map a filename from the API to a file inside the log folder, or None.
        Rejects anything that would escape the folder (e.g. '../../secret')."""
        candidate = (self._log_dir / filename).resolve()
        if candidate.parent != self._log_dir.resolve() or not candidate.is_file():
            return None
        return candidate

    @property
    def is_active(self) -> bool:
        return self._active

    @property
    def current_path(self) -> Optional[Path]:
        return self._session_path
