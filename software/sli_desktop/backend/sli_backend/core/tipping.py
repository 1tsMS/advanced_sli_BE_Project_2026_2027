"""
Tipping (stability) margin from the 4 outrigger FSRs.

Idea: the crane stands on 4 pads. The centre of pressure (CoP) of the 4 pad
forces is where the total weight (crane + load) acts on the ground. The crane
tips when the CoP reaches an edge of the support polygon (the pads). So:

    margin = how far the CoP is from the nearest edge, as a % of how far the
             polygon centre is from that edge (100 % = centred, 0 % = on the
             edge, i.e. a pad is about to lift off; negative = outside)

FSR -> force: each FSR is the top half of a voltage divider with a fixed
resistor to GND (reading rises with force). Then

    R_fsr = R_fixed * (4095 - adc) / adc   ->   G = 1/R_fsr ∝ adc / (4095 - adc)

and an FSR's conductance is roughly proportional to force. The CoP only needs
RELATIVE forces, so R_fixed cancels (same resistor on all 4 legs) and its
value is not needed. Per-leg gains (from a calibration with the crane at rest,
unloaded) even out sensor-to-sensor spread. Assumption behind that calibration:
the unloaded crane's CoP is at the polygon centre.

Monitoring only: the backend warns; nothing here stops a motor.
"""
from __future__ import annotations

import json
import logging
import math
from collections import deque
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Optional, Sequence

logger = logging.getLogger(__name__)

ADC_MAX = 4095


@dataclass
class TippingConfig:
    # Pad positions (mm) in the crane base frame, IN ORDER AROUND THE POLYGON,
    # index = FSR1..FSR4. Placeholder 200 x 200 mm square until measured.
    legs: list[list[float]] = field(default_factory=lambda: [
        [-100.0, -100.0], [100.0, -100.0], [100.0, 100.0], [-100.0, 100.0]])
    gains: list[float] = field(default_factory=lambda: [1.0, 1.0, 1.0, 1.0])
    minTotal: float = 0.05     # sum of conductance units below this = pads not loaded / FSRs off
    warnPct: float = 30.0      # margin below this = tipping warning
    calibrated: bool = False


def fsr_force(adc: int) -> float:
    """Force proxy (conductance, arbitrary units) from a raw 12-bit reading."""
    adc = max(0, min(int(adc), ADC_MAX - 1))
    return adc / (ADC_MAX - adc)


def _edge_distances(p: tuple[float, float], poly: Sequence[Sequence[float]]) -> list[float]:
    """Signed distance from p to each polygon edge, positive inside (any winding)."""
    n = len(poly)
    area2 = sum(poly[i][0] * poly[(i + 1) % n][1] - poly[(i + 1) % n][0] * poly[i][1] for i in range(n))
    sign = 1.0 if area2 > 0 else -1.0          # counter-clockwise: inside is to the left
    out = []
    for i in range(n):
        ax, ay = poly[i]
        bx, by = poly[(i + 1) % n]
        ex, ey = bx - ax, by - ay
        length = math.hypot(ex, ey) or 1e-9
        out.append(sign * (ex * (p[1] - ay) - ey * (p[0] - ax)) / length)
    return out


def compute(fsr: Sequence[int], cfg: TippingConfig) -> dict:
    """CoP + margin for one frame. valid=False when the pads carry ~nothing."""
    forces = [fsr_force(a) * g for a, g in zip(fsr, cfg.gains)]
    total = sum(forces)
    if total < cfg.minTotal:
        return {"valid": False, "marginPct": None, "copX": None, "copY": None,
                "shares": [0.0, 0.0, 0.0, 0.0], "warn": False}
    cx = sum(f * l[0] for f, l in zip(forces, cfg.legs)) / total
    cy = sum(f * l[1] for f, l in zip(forces, cfg.legs)) / total
    centre = (sum(l[0] for l in cfg.legs) / 4.0, sum(l[1] for l in cfg.legs) / 4.0)
    d_cop = _edge_distances((cx, cy), cfg.legs)
    d_ctr = _edge_distances(centre, cfg.legs)
    margin = min(dc / max(dm, 1e-9) for dc, dm in zip(d_cop, d_ctr)) * 100.0
    return {"valid": True, "marginPct": round(margin, 1), "copX": round(cx, 1), "copY": round(cy, 1),
            "shares": [round(f / total, 3) for f in forces], "warn": margin < cfg.warnPct}


def calibration_gains(samples: Sequence[Sequence[int]]) -> Optional[list[float]]:
    """Gains that make the 4 legs read equal on the averaged samples (crane at rest, no load)."""
    if not samples:
        return None
    mean = [sum(fsr_force(s[i]) for s in samples) / len(samples) for i in range(4)]
    if min(mean) <= 1e-4:
        return None                              # a pad reads nothing: can't calibrate
    avg = sum(mean) / 4.0
    return [round(avg / m, 4) for m in mean]


class TippingStore:
    """tipping.json next to the profiles: geometry, gains, warn level."""

    def __init__(self, path: Path) -> None:
        self._path = path
        self.cfg = TippingConfig()
        self.recent: deque = deque(maxlen=100)     # last ~2 s of raw FSR frames (calibration)
        self._load()

    def update(self, fsr: Sequence[int]) -> dict:
        """One telemetry frame in, its tipping result out."""
        self.recent.append(list(fsr))
        return compute(fsr, self.cfg)

    def _load(self) -> None:
        try:
            if self._path.is_file():
                data = json.loads(self._path.read_text(encoding="utf-8"))
                self.cfg = TippingConfig(**{**asdict(TippingConfig()), **data})
        except (OSError, ValueError, TypeError) as e:
            logger.warning(f"tipping.json unreadable, using defaults: {e}")

    def save(self, cfg: TippingConfig) -> None:
        if len(cfg.legs) != 4 or any(len(l) != 2 for l in cfg.legs) or len(cfg.gains) != 4:
            raise ValueError("need 4 legs [x, y] and 4 gains")
        self.cfg = cfg
        self._path.write_text(json.dumps(asdict(cfg), indent=2), encoding="utf-8")
