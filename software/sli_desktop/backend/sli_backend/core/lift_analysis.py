"""
Post-lift analysis: phases, load/dynamics metrics, operator score and cost.

Pure functions over the rows the LiftRecorder wrote (one per telemetry frame,
~50 Hz). Nothing here talks to hardware. Every number in the report is
traceable to a formula in this file, with the weights in AnalysisSettings, so
the score can be explained (and argued with) in the report and the paper.

Row keys used: t (s from lift start), load (kg), pct, capKg, state, mode,
m1..m4 (-1/0/+1: commanded direction per axis, 0 = idle), shaping (0/1),
tipValid, tipMargin.
"""
from __future__ import annotations

import statistics
from dataclasses import dataclass, field, asdict
from typing import Optional, Sequence

AXES = {1: "swing", 2: "boom", 3: "telescope", 4: "winch"}


@dataclass
class AnalysisSettings:
    # Load detection
    loadedMinKg: float = 0.05        # below this the hook is empty
    loadedFrac: float = 0.3          # ... or below this fraction of the static load
    # Dynamics
    snatchDaf: float = 1.30          # pick-up DAF above this = snatch
    rippleWindowS: float = 2.0       # load ripple measured this long after a stop
    # Penalty points (safety)
    pen: dict = field(default_factory=lambda: {
        "cutEvent": 10.0,      # each entry into CUT
        "warnEvent": 3.0,      # each entry into WARN
        "cutPerS": 2.0,        # per second spent in CUT
        "warnPerS": 0.5,       # per second spent in WARN
        "snatch": 5.0,         # each snatch pick-up
        "tipWarnPerS": 2.0,    # per second with the tipping margin under its warn level
        "refusal": 2.0,        # each motion the SLI refused (operator pushed into a limit)
        "estop": 3.0,          # each E-stop during the lift
        "sliNotArmed": 10.0,   # lift done (partly) with the SLI in BYPASS or OFF
    })
    safetyZeroAt: float = 50.0       # penalty points at which the safety score hits 0
    # Cost (model rates, edit in Settings)
    currency: str = "INR"
    ratePerMin: float = 50.0
    ratePerKg: float = 100.0
    ratePerPenaltyPoint: float = 5.0   # cost is mostly time + weight; penalties add on top


def _clamp01(x: float) -> float:
    return 0.0 if x < 0 else 1.0 if x > 1 else x


def _segments(rows: Sequence[dict], key) -> list[tuple[int, int, object]]:
    """Runs of equal key(row): (start index, end index exclusive, value)."""
    out, start = [], 0
    for i in range(1, len(rows) + 1):
        if i == len(rows) or key(rows[i]) != key(rows[start]):
            out.append((start, i, key(rows[start])))
            start = i
    return out


def _moving(r: dict) -> bool:
    return any(r.get(f"m{a}", 0) for a in AXES)


def _std_g(vals: Sequence[float]) -> float:
    return statistics.pstdev(vals) * 1000.0 if len(vals) > 1 else 0.0


def analyse(rows: Sequence[dict], events: Sequence[dict] = (), cfg: Optional[AnalysisSettings] = None,
            meta: Optional[dict] = None) -> dict:
    cfg = cfg or AnalysisSettings()
    meta = meta or {}
    if len(rows) < 2:
        return {"ok": False, "reason": "lift too short (no telemetry recorded)", **meta}

    t = [r["t"] for r in rows]
    load = [r["load"] for r in rows]
    dur = t[-1] - t[0]
    dt = dur / (len(rows) - 1) if len(rows) > 1 else 0.02

    # ---------------- load + pick-ups ----------------
    heavy = [x for x in load if x > cfg.loadedMinKg]
    static_all = statistics.median(heavy) if heavy else 0.0
    thr = max(cfg.loadedMinKg, cfg.loadedFrac * static_all)
    loaded = [x > thr for x in load]

    pickups = []
    for s, e, on in _segments(rows, lambda r: r["load"] > thr):
        if not on or (t[e - 1] - t[s]) < 1.0:
            continue
        settle = [load[i] for i in range(s, e) if t[s] + 1.5 <= t[i] <= t[s] + 4.0] or load[s:e]
        st = statistics.median(settle)
        peak_win = [load[i] for i in range(max(0, s - 10), e) if t[i] <= t[s] + 2.0]
        pk = max(peak_win) if peak_win else st
        daf = pk / st if st > 0 else 1.0
        pickups.append({"t": round(t[s], 2), "staticKg": round(st, 3), "peakKg": round(pk, 3),
                        "daf": round(daf, 3), "snatch": daf > cfg.snatchDaf,
                        "heldS": round(t[e - 1] - t[s], 1)})

    lifted_kg = max((p["staticKg"] for p in pickups), default=0.0)
    loaded_rows = [r for r, l in zip(rows, loaded) if l]
    caps = [r["capKg"] for r in loaded_rows if r.get("capKg", 0) > 0]
    pcts = [r["pct"] for r in loaded_rows if r.get("capKg", 0) > 0]

    # ---------------- phases ----------------
    def phase(i: int) -> str:
        r = rows[i]
        if not loaded[i]:
            return "approach" if _moving(r) else "idle"
        if r.get("m1", 0):
            return "slew"
        if r.get("m2", 0) or r.get("m3", 0) or r.get("m4", 0):
            return "hoist"
        return "hold"

    labels = [phase(i) for i in range(len(rows))]
    # pick-up / set-down: the moments the load comes on and goes off
    for p in pickups:
        for i in range(len(rows)):
            if p["t"] - 0.3 <= t[i] <= p["t"] + 1.0:
                labels[i] = "pick-up"
    for i in range(1, len(rows)):
        if loaded[i - 1] and not loaded[i]:
            for j in range(max(0, i - int(0.8 / max(dt, 1e-3))), min(len(rows), i + int(0.3 / max(dt, 1e-3)))):
                labels[j] = "set-down"
    phases = []
    for s, e, name in _segments(list(range(len(rows))), lambda i: labels[i]):
        if phases and phases[-1]["name"] == name:
            phases[-1]["t1"] = round(t[e - 1], 2)
        elif phases and (t[e - 1] - t[s]) < 0.3:          # merge flicker into the previous phase
            phases[-1]["t1"] = round(t[e - 1], 2)
        else:
            phases.append({"name": name, "t0": round(t[s], 2), "t1": round(t[e - 1], 2)})
    totals: dict[str, float] = {}
    for p in phases:
        totals[p["name"]] = round(totals.get(p["name"], 0.0) + (p["t1"] - p["t0"]), 2)

    # ---------------- safety ----------------
    states = _segments(rows, lambda r: r.get("state", ""))
    warn_entries = sum(1 for k, (s, e, v) in enumerate(states) if v == "WARN" and k > 0)
    cut_entries = sum(1 for k, (s, e, v) in enumerate(states) if v == "CUT" and k > 0)
    if states and states[0][2] in ("WARN", "CUT"):          # started already in it
        warn_entries += states[0][2] == "WARN"
        cut_entries += states[0][2] == "CUT"
    warn_s = sum(dt for r in rows if r.get("state") == "WARN")
    cut_s = sum(dt for r in rows if r.get("state") == "CUT")
    tip_vals = [r["tipMargin"] for r in rows if r.get("tipValid")]
    tip_warn_level = meta.get("tipWarnPct", 30.0)
    tip_warn_s = sum(dt for r in rows if r.get("tipValid") and r["tipMargin"] < tip_warn_level)
    modes = sorted({r.get("mode", "ARMED") for r in rows})
    not_armed = any(m != "ARMED" for m in modes)
    # One refusal per episode: the same axis/dir refused again within 1 s is the
    # operator still holding the button (the firmware ACKs once, a repeat-ACK must not double-count)
    refusals, last_ref = [], {}
    for e in events:
        if e.get("kind") != "refused":
            continue
        key = e.get("text", "").split(",")[:2]
        k = tuple(key)
        if k not in last_ref or e["t"] - last_ref[k] > 1.0:
            refusals.append(e)
        last_ref[k] = e["t"]
    cuts_ack = [e for e in events if e.get("kind") == "cut"]
    estops = [e for e in events if e.get("kind") == "estop"]
    snatches = [p for p in pickups if p["snatch"]]

    P = cfg.pen
    items = [
        ("Entries into CUT", cut_entries, P["cutEvent"] * cut_entries),
        ("Entries into WARN", warn_entries, P["warnEvent"] * warn_entries),
        ("Seconds in CUT", round(cut_s, 1), P["cutPerS"] * cut_s),
        ("Seconds in WARN", round(warn_s, 1), P["warnPerS"] * warn_s),
        ("Snatch pick-ups", len(snatches), P["snatch"] * len(snatches)),
        ("Seconds near tipping", round(tip_warn_s, 1), P["tipWarnPerS"] * tip_warn_s),
        ("Motions refused by SLI", len(refusals), P["refusal"] * len(refusals)),
        ("E-stops", len(estops), P["estop"] * len(estops)),
        ("SLI not armed", int(not_armed), P["sliNotArmed"] * int(not_armed)),
    ]
    points = sum(p for _, _, p in items)

    # ---------------- smoothness ----------------
    jogs = reversals = 0
    stops = []
    for a in AXES:
        key = f"m{a}"
        last_dir, last_stop_t = 0, -1e9
        for i in range(1, len(rows)):
            prev, cur = rows[i - 1].get(key, 0), rows[i].get(key, 0)
            if prev == cur:
                continue
            if cur != 0:
                jogs += 1
                if last_dir and cur == -last_dir and (prev != 0 or t[i] - last_stop_t < 1.0):
                    reversals += 1
                last_dir = cur
            if prev != 0 and cur == 0:
                last_stop_t = t[i]
                if loaded[i]:
                    w = [load[j] for j in range(i, len(rows))
                         if t[i] + 0.2 <= t[j] <= t[i] + 0.2 + cfg.rippleWindowS
                         and not _moving(rows[j]) and loaded[j]]
                    if len(w) * dt >= 1.0:
                        stops.append({"t": round(t[i], 2), "axis": AXES[a], "rippleG": round(_std_g(w), 1),
                                      "shaped": bool(rows[i].get("shaping", 0)) and a == 1})
    minutes = max(dur / 60.0, 1e-6)
    ripple = statistics.mean(s["rippleG"] for s in stops) if stops else 0.0
    swing_stops = [s for s in stops if s["axis"] == "swing"]
    daf_max = max((p["daf"] for p in pickups), default=1.0)

    s_daf = _clamp01(1.0 - (daf_max - 1.05) / 0.5)          # 1.05 -> full marks, 1.55 -> none
    s_rip = _clamp01(1.0 - ripple / 50.0)                    # 0 g -> full, 50 g ripple -> none
    s_rev = _clamp01(1.0 - (reversals / minutes) / 10.0)     # 10 reversals/min -> none
    smooth = 35.0 * (0.5 * s_daf + 0.25 * s_rip + 0.25 * s_rev)

    # ---------------- efficiency ----------------
    idle_s = sum(dt for r, l in zip(rows, loaded) if not _moving(r))
    idle_frac = idle_s / dur if dur > 0 else 0.0
    s_idle = _clamp01(1.0 - idle_frac / 0.6)                 # 60 % idle -> none
    s_jog = _clamp01(1.0 - (jogs / minutes - 6.0) / 24.0)    # <= 6 starts/min full, 30 -> none
    eff = 25.0 * (0.6 * s_idle + 0.4 * s_jog)

    safety = 40.0 * _clamp01(1.0 - points / cfg.safetyZeroAt)
    total = safety + smooth + eff
    grade = "A" if total >= 85 else "B" if total >= 70 else "C" if total >= 55 else "D"

    # ---------------- cost ----------------
    c_time = minutes * cfg.ratePerMin
    c_load = lifted_kg * cfg.ratePerKg
    c_pen = points * cfg.ratePerPenaltyPoint

    # ---------------- flags (timeline notes for the report) ----------------
    flags = []
    for p in snatches:
        flags.append({"t": p["t"], "level": "warn", "text": f"Snatch pick-up: DAF {p['daf']:.2f} "
                      f"(peak {p['peakKg']*1000:.0f} g on {p['staticKg']*1000:.0f} g)"})
    for k, (s, e, v) in enumerate(states):
        if v in ("WARN", "CUT") and (k > 0 or s == 0):
            flags.append({"t": round(t[s], 2), "level": "crit" if v == "CUT" else "warn",
                          "text": f"SLI {v} for {t[e-1]-t[s]:.1f} s (peak {max(r['pct'] for r in rows[s:e]):.0f} %)"})
    for e in refusals:
        flags.append({"t": e["t"], "level": "warn", "text": f"Refused by SLI: {e.get('text', '')}"})
    for e in cuts_ack:
        flags.append({"t": e["t"], "level": "crit", "text": f"Motion cut by SLI: {e.get('text', '')}"})
    for e in estops:
        flags.append({"t": e["t"], "level": "crit", "text": "E-stop"})
    if not_armed:
        flags.append({"t": 0.0, "level": "warn", "text": f"SLI mode during lift: {', '.join(modes)}"})
    if not pickups:
        flags.append({"t": 0.0, "level": "info", "text": "No load detected on the hook"})
    flags.sort(key=lambda f: f["t"])

    return {
        "ok": True, **meta,
        "durationS": round(dur, 2), "samples": len(rows), "rateHz": round(1.0 / dt, 1) if dt > 0 else 0.0,
        "load": {
            "liftedKg": round(lifted_kg, 3),
            "peakKg": round(max(load), 3),
            "ratedKg": round(statistics.median(caps), 3) if caps else None,
            "meanPct": round(statistics.mean(pcts), 1) if pcts else None,
            "peakPct": round(max(pcts), 1) if pcts else None,
        },
        "pickups": pickups,
        "phases": phases, "phaseTotals": totals,
        "safety": {
            "warnEntries": warn_entries, "cutEntries": cut_entries,
            "warnS": round(warn_s, 1), "cutS": round(cut_s, 1),
            "tipWarnS": round(tip_warn_s, 1),
            "minTipMargin": round(min(tip_vals), 1) if tip_vals else None,
            "modes": modes, "refusals": len(refusals), "estops": len(estops),
        },
        "smoothness": {
            "dafMax": round(daf_max, 3), "jogs": jogs, "jogsPerMin": round(jogs / minutes, 1),
            "reversals": reversals, "reversalsPerMin": round(reversals / minutes, 1),
            "meanRippleG": round(ripple, 1),
            "swingStopRippleG": round(statistics.mean(s["rippleG"] for s in swing_stops), 1) if swing_stops else None,
            "stops": stops,
        },
        "efficiency": {"idleS": round(idle_s, 1), "idleFrac": round(idle_frac, 3)},
        "penalty": {"points": round(points, 1),
                    "items": [{"what": w, "value": v, "points": round(p, 1)} for w, v, p in items if p > 0]},
        "score": {
            "total": round(total, 1), "grade": grade,
            "safety": round(safety, 1), "smoothness": round(smooth, 1), "efficiency": round(eff, 1),
            "max": {"safety": 40, "smoothness": 35, "efficiency": 25},
            "parts": {"daf": round(s_daf, 2), "ripple": round(s_rip, 2), "reversals": round(s_rev, 2),
                      "idle": round(s_idle, 2), "jogRate": round(s_jog, 2)},
        },
        "cost": {
            "currency": cfg.currency,
            "time": round(c_time, 2), "load": round(c_load, 2), "penalty": round(c_pen, 2),
            "total": round(c_time + c_load + c_pen, 2),
            "rates": {"perMin": cfg.ratePerMin, "perKg": cfg.ratePerKg, "perPenaltyPoint": cfg.ratePerPenaltyPoint},
        },
        "flags": flags,
        "settings": asdict(cfg),
    }


def downsample(rows: Sequence[dict], n: int, key: str = "load") -> list[dict]:
    """Largest-Triangle-Three-Buckets on `key`: keeps the shape (and the peaks) of the
    load trace with n points; the other columns are taken at the same rows."""
    if n >= len(rows) or n < 3:
        return list(rows)
    out = [rows[0]]
    every = (len(rows) - 2) / (n - 2)
    a = 0
    for i in range(n - 2):
        lo, hi = int(i * every) + 1, int((i + 1) * every) + 1
        nlo, nhi = hi, min(int((i + 2) * every) + 1, len(rows))
        avg_t = sum(r["t"] for r in rows[nlo:nhi]) / max(nhi - nlo, 1)
        avg_v = sum(r[key] for r in rows[nlo:nhi]) / max(nhi - nlo, 1)
        ax, ay = rows[a]["t"], rows[a][key]
        best, best_area = lo, -1.0
        for j in range(lo, min(hi, len(rows))):
            area = abs((ax - avg_t) * (rows[j][key] - ay) - (ax - rows[j]["t"]) * (avg_v - ay))
            if area > best_area:
                best, best_area = j, area
        out.append(rows[best])
        a = best
    out.append(rows[-1])
    return out
