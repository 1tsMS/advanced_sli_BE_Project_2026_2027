"""
Load chart profiles: validation, JSON storage on the PC, and the LC command
lines that upload a profile to the ESP32.

The ESP32 is the authority (it validates again on LC COMMIT and enforces the
chart); the limits here mirror firmware load_chart.h so a bad profile is
caught in the dashboard instead of as a cryptic $ACK,LC,ERR.
"""
from __future__ import annotations
import json
import math
import re
from datetime import datetime
from pathlib import Path
from typing import Optional

from ..api.models import LoadChartProfile

# Mirrors firmware load_chart.h
MAX_ANGLES = 8
MAX_EXTS = 6
MAX_ZONES = 4
MAX_CAP_KG = 10.0
NAME_RE = re.compile(r"^[\w .-]{1,15}$", re.ASCII)   # LC_NAME_LEN 16 incl. the terminator: ASCII only
MIN_CAP_KG = 0.001   # smallest value that survives the 3-decimal upload format


def validate(p: LoadChartProfile) -> Optional[str]:
    """None if the profile would pass the firmware's lc_validate, else the reason."""
    if not NAME_RE.match(p.name) or p.name != p.name.strip():
        return "Name: 1-15 letters, digits, space, . _ - (no leading/trailing space)"
    a, e = p.anglesDeg, p.extensionsMM
    if not (2 <= len(a) <= MAX_ANGLES):
        return f"Need 2-{MAX_ANGLES} boom angles"
    if not (2 <= len(e) <= MAX_EXTS):
        return f"Need 2-{MAX_EXTS} extensions"
    if not all(math.isfinite(v) for v in a + e):
        return "Angles and extensions must be numbers"
    if any(a[i + 1] <= a[i] for i in range(len(a) - 1)):
        return "Boom angles must increase left to right"
    if any(e[i + 1] <= e[i] for i in range(len(e) - 1)):
        return "Extensions must increase"
    if a[0] < -90 or a[-1] > 90:
        return "Boom angles must be within -90..90 deg"
    if len(p.capacityKg) != len(a) or any(len(r) != len(e) for r in p.capacityKg):
        return "Capacity table must have one row per angle and one column per extension"
    for r in p.capacityKg:
        for c in r:
            if not (math.isfinite(c) and MIN_CAP_KG <= c <= MAX_CAP_KG):
                return f"Every capacity must be {MIN_CAP_KG:g}-{MAX_CAP_KG:g} kg"
    if len(p.zones) > MAX_ZONES:
        return f"At most {MAX_ZONES} swing zones"
    for z in p.zones:
        if not (0 <= z.fromDeg < 360 and 0 <= z.toDeg <= 360) or z.fromDeg == z.toDeg:
            return "Zone angles must be 0-360 and not equal"
        if not (0 < z.factor <= 1.5):
            return "Zone factor must be > 0 and <= 1.5"
    if not (0 < p.cutPct <= 150):
        return "Cut % must be > 0 and <= 150"
    if not (0 < p.warnPct < p.cutPct):
        return "Warn % must be > 0 and below cut %"
    return None


def _num(v: float) -> str:
    """Compact number for the 128-char command line ("0.6", not "0.600000")."""
    return f"{v:.3f}".rstrip("0").rstrip(".") if v != int(v) else str(int(v))


def to_commands(p: LoadChartProfile) -> list[str]:
    """LC lines in upload order, ending with COMMIT (each matches command_router's whitelist)."""
    cmds = [f"LC BEGIN {len(p.anglesDeg)} {len(p.extensionsMM)} {p.name}",
            "LC A " + " ".join(_num(v) for v in p.anglesDeg),
            "LC E " + " ".join(_num(v) for v in p.extensionsMM)]
    for i, row in enumerate(p.capacityKg):
        cmds.append(f"LC R {i} " + " ".join(_num(v) for v in row))
    for z in p.zones:
        cmds.append(f"LC Z {_num(z.fromDeg)} {_num(z.toDeg)} {_num(z.factor)}")
    cmds.append(f"LC P {_num(p.warnPct)} {_num(p.cutPct)}")
    cmds.append("LC COMMIT")
    return cmds


class ChartReadback:
    """Collects $LC lines (reply to LC GET) into a profile-shaped dict."""

    def __init__(self) -> None:
        self._cur: Optional[dict] = None

    def feed(self, line: str) -> Optional[dict]:
        """Returns the finished chart dict on $LC,END, {"none": True} on $LC,NONE, else None."""
        parts = line.strip().split(",")
        tag = parts[1] if len(parts) > 1 else ""
        if tag == "NONE":
            self._cur = None
            return {"none": True}
        if tag == "NAME":
            self._cur = {"name": ",".join(parts[2:]), "anglesDeg": [], "extensionsMM": [],
                         "capacityKg": [], "zones": [], "warnPct": 90.0, "cutPct": 100.0}
            return None
        if self._cur is None:
            return None
        try:
            if tag == "A":
                self._cur["anglesDeg"] = [float(v) for v in parts[2:]]
            elif tag == "E":
                self._cur["extensionsMM"] = [float(v) for v in parts[2:]]
            elif tag == "R":
                self._cur["capacityKg"].append([float(v) for v in parts[3:]])
            elif tag == "Z":
                f, t, k = (float(v) for v in parts[2:5])
                self._cur["zones"].append({"fromDeg": f, "toDeg": t, "factor": k})
            elif tag == "P":
                self._cur["warnPct"], self._cur["cutPct"] = float(parts[2]), float(parts[3])
            elif tag == "END":
                done, self._cur = self._cur, None
                return done
        except (ValueError, IndexError):
            self._cur = None     # garbled readback: drop it, the user can read again
        return None


class ProfileStore:
    """One JSON file per profile in a folder next to the logs."""

    def __init__(self, folder: Path) -> None:
        self._dir = folder
        self._dir.mkdir(parents=True, exist_ok=True)

    def _path(self, name: str) -> Path:
        if not NAME_RE.match(name):
            raise ValueError("bad profile name")
        return self._dir / (name.replace(" ", "_") + ".json")

    def list(self) -> list[LoadChartProfile]:
        out = []
        for f in sorted(self._dir.glob("*.json")):
            try:
                out.append(LoadChartProfile(**json.loads(f.read_text(encoding="utf-8"))))
            except Exception:
                continue          # a hand-broken file must not hide the others
        return out

    def get(self, name: str) -> Optional[LoadChartProfile]:
        f = self._path(name)
        if not f.exists():
            return None
        return LoadChartProfile(**json.loads(f.read_text(encoding="utf-8")))

    def save(self, p: LoadChartProfile) -> LoadChartProfile:
        p = p.model_copy(update={"updated": datetime.now().isoformat(timespec="seconds")})
        self._path(p.name).write_text(json.dumps(p.model_dump(), indent=2), encoding="utf-8")
        return p

    def delete(self, name: str) -> bool:
        f = self._path(name)
        if not f.exists():
            return False
        f.unlink()
        return True
