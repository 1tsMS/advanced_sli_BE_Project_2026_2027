"""
LiftRecorder — one lift = Start Lift ... Stop Lift on the dashboard.

While a lift runs, every telemetry frame becomes a row of
logs/lift_<YYYYmmdd_HHMMSS>.csv, enriched with what the backend knows and the
ESP32 frame doesn't carry: the latest SLI status ($S), the motion each axis is
being commanded (from the commands the backend itself sent), swing shaping
on/off and the tipping margin. SLI acks (cut / refused) and E-stops are kept
as events. On stop the lift is analysed (lift_analysis.py) and the result is
saved next to the CSV as lift_<...>.json.

Also owns operators.json (operator dropdown) and lift_settings.json (score
weights + cost rates) in the profiles folder.
"""
from __future__ import annotations

import csv
import json
import logging
import re
import time
from dataclasses import asdict
from datetime import datetime
from pathlib import Path
from typing import IO, Optional

from ..api.models import TelemetryFrame, SafetyStatus
from . import lift_analysis
from .lift_analysis import AnalysisSettings

logger = logging.getLogger(__name__)

DEFAULT_OPERATORS = ["MS", "Ali", "Aayush"]
MOTION_TIMEOUT_S = 0.4          # same dead-man as the firmware: a held motion repeats every 100 ms

LIFT_HEADER = [
    "t", "timestamp", "load", "pct", "capKg", "state", "mode", "blocked",
    "boom", "ext", "swing", "rope", "hookDrop", "imuTilt",
    "fsr1", "fsr2", "fsr3", "fsr4", "tipValid", "tipMargin", "copX", "copY",
    "m1", "m2", "m3", "m4", "shaping",
]
_MOTOR = re.compile(r"^M([1-4])\s+S(\d+)\s+D([01])$")
_STOP = re.compile(r"^M0\s+A([1-4])")
_LIFT_ID = re.compile(r"^lift_\d{8}_\d{6}$")


class MotionTracker:
    """Which way each axis is being driven right now, from the commands sent."""

    def __init__(self) -> None:
        self._dir = {a: 0 for a in range(1, 5)}
        self._last = {a: 0.0 for a in range(1, 5)}

    def on_command(self, cmd: str, now: float) -> None:
        m = _MOTOR.match(cmd)
        if m:
            a, s, d = int(m[1]), int(m[2]), int(m[3])
            self._dir[a] = (1 if d else -1) if s > 0 else 0
            self._last[a] = now
            return
        m = _STOP.match(cmd)
        if m:
            self._dir[int(m[1])] = 0
        elif cmd == "T0":
            for a in self._dir:
                self._dir[a] = 0

    def stop(self, axis: int) -> None:
        self._dir[axis] = 0

    def state(self, now: float) -> dict[int, int]:
        return {a: (d if now - self._last[a] <= MOTION_TIMEOUT_S else 0) for a, d in self._dir.items()}


class LiftRecorder:
    def __init__(self, log_dir: Path, config_dir: Path) -> None:
        self._log_dir = log_dir
        self._ops_path = config_dir / "operators.json"
        self._settings_path = config_dir / "lift_settings.json"
        self.motion = MotionTracker()
        self.safety = SafetyStatus()
        self.shaping = False
        self._file: Optional[IO] = None
        self._writer: Optional[csv.DictWriter] = None
        self._rows: list[dict] = []
        self._events: list[dict] = []
        self._id: Optional[str] = None
        self._operator = ""
        self._note = ""
        self._t0 = 0.0
        self.tip_warn_pct = 30.0

    # ---------------- operators + settings ----------------
    def operators(self) -> list[str]:
        try:
            ops = json.loads(self._ops_path.read_text(encoding="utf-8"))
            if isinstance(ops, list) and all(isinstance(o, str) for o in ops) and ops:
                return ops
        except (OSError, ValueError):
            pass
        return list(DEFAULT_OPERATORS)

    def add_operator(self, name: str) -> list[str]:
        name = name.strip()
        if not re.fullmatch(r"[\w .'-]{1,24}", name):
            raise ValueError("Operator name: 1-24 letters, digits, spaces, . ' -")
        ops = self.operators()
        if name.lower() not in (o.lower() for o in ops):
            ops.append(name)
            self._ops_path.parent.mkdir(parents=True, exist_ok=True)
            self._ops_path.write_text(json.dumps(ops, indent=2), encoding="utf-8")
        return ops

    def settings(self) -> AnalysisSettings:
        try:
            data = json.loads(self._settings_path.read_text(encoding="utf-8"))
            base = asdict(AnalysisSettings())
            base.update({k: v for k, v in data.items() if k in base and k != "pen"})
            base["pen"] = {**base["pen"], **{k: float(v) for k, v in data.get("pen", {}).items() if k in base["pen"]}}
            return AnalysisSettings(**base)
        except (OSError, ValueError, TypeError):
            return AnalysisSettings()

    def save_settings(self, data: dict) -> AnalysisSettings:
        base = asdict(self.settings())
        for k, v in data.items():
            if k == "pen" and isinstance(v, dict):
                base["pen"].update({pk: float(pv) for pk, pv in v.items() if pk in base["pen"]})
            elif k in base and k != "pen":
                base[k] = type(base[k])(v)
        cfg = AnalysisSettings(**base)
        self._settings_path.parent.mkdir(parents=True, exist_ok=True)
        self._settings_path.write_text(json.dumps(asdict(cfg), indent=2), encoding="utf-8")
        return cfg

    # ---------------- live inputs ----------------
    def on_command(self, cmd: str) -> None:
        now = time.time()
        self.motion.on_command(cmd, now)
        if self.active and cmd == "T0":
            self._events.append({"t": round(now - self._t0, 2), "kind": "estop"})

    def on_ack(self, line: str) -> None:
        # $ACK,SLI,CUT,A1,D0,<why>  /  $ACK,SLI,REFUSED,...  /  $ACK,ESTOP
        parts = line.split(",")
        if len(parts) >= 5 and parts[1] == "SLI" and parts[2] in ("CUT", "REFUSED"):
            try:
                self.motion.stop(int(parts[3][1:]))        # the ESP32 stopped that axis
            except ValueError:
                pass
            if self.active:
                self._events.append({"t": round(time.time() - self._t0, 2),
                                     "kind": "cut" if parts[2] == "CUT" else "refused",
                                     "text": ",".join(parts[3:])})

    def on_safety(self, status: SafetyStatus) -> None:
        self.safety = status

    def on_frame(self, frame: TelemetryFrame, tip: dict) -> None:
        if not self.active:
            return
        m = self.motion.state(frame.timestamp)
        row = {
            "t": round(frame.timestamp - self._t0, 3), "timestamp": frame.timestamp,
            "load": round(frame.actualLoad, 4), "pct": round(frame.loadPercent, 1),
            "capKg": round(frame.safeLoadLimit, 3), "state": self.safety.state, "mode": self.safety.mode,
            "blocked": self.safety.blocked,
            "boom": round(frame.boomAngle, 2), "ext": round(frame.extensionMM, 1),
            "swing": round(frame.swingAngle, 2), "rope": round(frame.ropeLength, 1),
            "hookDrop": round(frame.hookDrop, 1), "imuTilt": round(frame.imuPitch, 2),
            "fsr1": frame.fsr[0], "fsr2": frame.fsr[1], "fsr3": frame.fsr[2], "fsr4": frame.fsr[3],
            "tipValid": int(bool(tip.get("valid"))),
            "tipMargin": tip.get("marginPct") if tip.get("valid") else "",
            "copX": tip.get("copX") if tip.get("valid") else "",
            "copY": tip.get("copY") if tip.get("valid") else "",
            "m1": m[1], "m2": m[2], "m3": m[3], "m4": m[4], "shaping": int(self.shaping),
        }
        self._rows.append(row)
        try:
            self._writer.writerow(row)
            if len(self._rows) % 50 == 0:
                self._file.flush()
        except Exception as e:                       # disk full etc.: keep the lift in memory
            logger.error(f"Lift CSV write error: {e}")

    # ---------------- start / stop ----------------
    @property
    def active(self) -> bool:
        return self._id is not None

    def start(self, operator: str, note: str = "") -> dict:
        if self.active:
            return self.state()
        operator = operator.strip() or "Unknown"
        self._log_dir.mkdir(parents=True, exist_ok=True)
        self._id = "lift_" + datetime.now().strftime("%Y%m%d_%H%M%S")
        self._operator, self._note = operator, note.strip()[:200]
        self._t0 = time.time()
        self._rows, self._events = [], []
        self._file = open(self._log_dir / f"{self._id}.csv", "w", newline="", encoding="utf-8")
        self._writer = csv.DictWriter(self._file, fieldnames=LIFT_HEADER)
        self._writer.writeheader()
        logger.info(f"Lift started: {self._id} ({operator})")
        return self.state()

    def stop(self) -> Optional[dict]:
        """Close the lift, analyse it, save the JSON report. None if no lift was running."""
        if not self.active:
            return None
        lift_id = self._id
        if self._file:
            self._file.close()
        self._file, self._writer = None, None
        meta = {"id": lift_id, "operator": self._operator, "note": self._note,
                "startedAt": datetime.fromtimestamp(self._t0).isoformat(timespec="seconds"),
                "tipWarnPct": self.tip_warn_pct}
        rows = [{**r, "tipMargin": float(r["tipMargin"]) if r["tipValid"] else 0.0} for r in self._rows]
        report = lift_analysis.analyse(rows, self._events, self.settings(), meta)
        report["events"] = self._events
        (self._log_dir / f"{lift_id}.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
        logger.info(f"Lift stopped: {lift_id}, {len(rows)} rows, score {report.get('score', {}).get('total')}")
        self._id = None
        self._rows, self._events = [], []
        return report

    def state(self) -> dict:
        return {"active": self.active, "id": self._id, "operator": self._operator if self.active else None,
                "elapsedS": round(time.time() - self._t0, 1) if self.active else 0.0,
                "rows": len(self._rows) if self.active else 0, "shaping": self.shaping}

    # ---------------- saved lifts ----------------
    def _path(self, lift_id: str, ext: str) -> Optional[Path]:
        if not _LIFT_ID.match(lift_id):
            return None
        p = self._log_dir / f"{lift_id}.{ext}"
        return p if p.is_file() else None

    def list_lifts(self) -> list[dict]:
        out = []
        for f in sorted(self._log_dir.glob("lift_*.json"), reverse=True):
            try:
                r = json.loads(f.read_text(encoding="utf-8"))
            except (OSError, ValueError):
                continue
            out.append({"id": r.get("id", f.stem), "operator": r.get("operator"), "note": r.get("note", ""),
                        "startedAt": r.get("startedAt"), "durationS": r.get("durationS"),
                        "liftedKg": (r.get("load") or {}).get("liftedKg"),
                        "score": (r.get("score") or {}).get("total"), "grade": (r.get("score") or {}).get("grade"),
                        "cost": (r.get("cost") or {}).get("total"), "ok": r.get("ok", False)})
        return out

    def report(self, lift_id: str) -> Optional[dict]:
        p = self._path(lift_id, "json")
        return json.loads(p.read_text(encoding="utf-8")) if p else None

    def rows(self, lift_id: str) -> Optional[list[dict]]:
        p = self._path(lift_id, "csv")
        if not p:
            return None
        out = []
        with open(p, newline="", encoding="utf-8") as f:
            for r in csv.DictReader(f):
                row = {}
                for k, v in r.items():
                    if k in ("state", "mode"):
                        row[k] = v
                    else:
                        try:
                            row[k] = float(v) if v != "" else None
                        except ValueError:
                            row[k] = v
                for a in (1, 2, 3, 4):
                    row[f"m{a}"] = int(row.get(f"m{a}") or 0)
                out.append(row)
        return out

    def reanalyse(self, lift_id: str) -> Optional[dict]:
        """Re-run the analysis (e.g. after the score weights or rates changed)."""
        old, rows = self.report(lift_id), self.rows(lift_id)
        if old is None or rows is None:
            return None
        meta = {k: old.get(k) for k in ("id", "operator", "note", "startedAt", "tipWarnPct")}
        clean = [{**r, "load": r["load"] or 0.0, "pct": r["pct"] or 0.0, "capKg": r["capKg"] or 0.0,
                  "tipValid": int(r.get("tipValid") or 0), "tipMargin": r.get("tipMargin") or 0.0} for r in rows]
        report = lift_analysis.analyse(clean, old.get("events", []), self.settings(), meta)
        report["events"] = old.get("events", [])
        (self._log_dir / f"{lift_id}.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
        return report

    def delete(self, lift_id: str) -> bool:
        found = False
        for ext in ("csv", "json"):
            p = self._path(lift_id, ext)
            if p:
                p.unlink()
                found = True
        return found
