"""
2026-11-07 firmware additions: boom-tip acceleration / jerk in $T, cut mode and
driver idle in $S, timestamped $EV lines, the new commands, and the shock list
in the lift analysis.

Run from software/sli_desktop/backend:
    python -m pytest -q
"""
import pytest

from sli_backend.core.packet_parser import PacketParser
from sli_backend.core.command_router import CommandRouter
from sli_backend.core.lift_analysis import analyse, find_shocks, AnalysisSettings
from sli_backend.core.lift_recorder import LiftRecorder
from sli_backend.api.models import TelemetryFrame

T_BASE = "$T,12.50,40.0,0.512,0.512,3.00,120.0,1000,1100,1200,1300,12.40,0.30,0.800,64.0,0,455.0,210.0"


def test_telemetry_20_fields():
    p = PacketParser()
    f = p._parse_telemetry(T_BASE + ",2.35,180.5,17")
    assert f.accelDyn == pytest.approx(2.35)
    assert f.jerk == pytest.approx(180.5)
    assert f.imuRejected == 17
    assert f.hookDrop == pytest.approx(210.0)
    old = p._parse_telemetry(T_BASE)                    # 17-field firmware still parses
    assert old.accelDyn == 0.0 and old.imuRejected == 0
    assert p._parse_telemetry(T_BASE + ",2.35") is None  # broken line: rejected


def test_safety_line_cut_mode_and_driver_idle():
    s = PacketParser.parse_safety("$S,WARN,92.1,0.600,-1,0,04,Lift limit,ARMED,-,R,0")
    assert s.cutMode == "R" and s.drvIdle is False and s.why == "-"
    old = PacketParser.parse_safety("$S,OK,10.0,0.600,-1,0,00,Lift limit,ARMED,-")
    assert old.cutMode == "P" and old.drvIdle is None
    assert PacketParser.parse_safety("$S,OK,10.0,0.600,-1,0,00,X,ARMED,-,R") is None   # 11 fields: invalid


def test_event_line_reaches_the_recorder(tmp_path):
    got = []
    p = PacketParser()
    p.set_callbacks(on_event=got.append)
    p.parse_line("$EV,123456,FI,1")
    p.parse_line("$EV,123512,SLI,FAULT,0.0")
    assert got[0] == {"fwMs": 123456, "kind": "FI", "detail": "1"}
    assert got[1]["detail"] == "FAULT,0.0"

    rec = LiftRecorder(tmp_path, tmp_path)
    rec.start("MS", "E6-T01")
    for ev in got:
        rec.on_event(ev)
    rec.on_frame(TelemetryFrame(timestamp=1.0, accelDyn=0.4, jerk=20.0), {"valid": False})
    rec.on_frame(TelemetryFrame(timestamp=1.02), {"valid": False})
    report = rec.stop()
    evs = [e for e in report["events"] if e["kind"] == "ev"]
    assert [e["ev"] for e in evs] == ["FI", "SLI"]
    assert evs[1]["fwMs"] - evs[0]["fwMs"] == 56          # firmware-clock latency survives
    rows = rec.rows(report["id"])
    assert rows[0]["accelDyn"] == pytest.approx(0.4) and rows[0]["cutMode"] == "P"


@pytest.mark.parametrize("cmd,ok", [
    ("SLI CUT P", True), ("SLI CUT R", True), ("SLI CUT S", True), ("SLI CUT X", False),
    ("DRV IDLE 0", True), ("DRV IDLE 1", True), ("DRV IDLE 2", False), ("DRV 1", False),
])
def test_new_commands_whitelisted(cmd, ok):
    assert CommandRouter(serial=None)._is_valid(cmd) is ok


def _rows(spikes, rate=50.0, dur=6.0):
    """Quiet lift with boom moving up 1-2 s, load on the hook from 3 s; spikes = {t: accel}."""
    rows = []
    for i in range(int(dur * rate)):
        t = i / rate
        a = next((v for ts, v in spikes.items() if abs(t - ts) < 0.5 / rate), 0.05)
        rows.append({"t": t, "load": 0.5 if t >= 3.0 else 0.0, "pct": 50.0, "capKg": 1.0,
                     "state": "OK", "mode": "ARMED", "m1": 0, "m2": 1 if 1.0 <= t < 2.0 else 0,
                     "m3": 0, "m4": 0, "shaping": 0, "tipValid": 0, "tipMargin": 0.0,
                     "accelDyn": a, "jerk": a * 100.0})
    return rows


def test_shocks_are_found_and_explained():
    rows = _rows({2.1: 2.5, 2.16: 1.8, 3.0: 1.5, 5.0: 3.0})
    r = analyse(rows, [], AnalysisSettings())
    shocks = r["dynamics"]["shocks"]
    assert [s["cause"] for s in shocks] == ["boom stop", "lift-off", "unexplained"]
    assert shocks[0]["accel"] == pytest.approx(2.45)        # two close peaks = one shock, height above the 0.05 idle
    assert shocks[0]["jerk"] == pytest.approx(250.0)
    assert r["dynamics"]["peakAccel"] == pytest.approx(3.0)
    assert any("Boom-tip shock" in f["text"] for f in r["flags"])


def test_no_shocks_without_imu_columns():
    rows = _rows({})
    for row in rows:
        row.pop("accelDyn"), row.pop("jerk")
    r = analyse(rows, [], AnalysisSettings())
    assert r["dynamics"] == {"peakAccel": None, "peakJerk": None, "shocks": []}
    assert find_shocks(rows, [], [False] * len(rows), [], AnalysisSettings()) == []


def test_shocks_ignore_a_constant_offset():
    # IMU scale error: everything reads 1.7 m/s^2 higher, shocks still found relative to it
    rows = _rows({2.1: 2.5, 5.0: 3.0})
    for r in rows:
        r["accelDyn"] += 1.7
    shocks = analyse(rows, [], AnalysisSettings())["dynamics"]["shocks"]
    assert len(shocks) == 2
    assert shocks[0]["accel"] == pytest.approx(2.45, abs=0.01)   # height above the 0.05 idle level


def test_pad_kg_estimate():
    from sli_backend.core.tipping import pad_kg
    assert pad_kg([0.25, 0.25, 0.25, 0.25], 0.0, 0.5) is None            # crane weight unknown
    assert pad_kg([0.4, 0.3, 0.2, 0.1], 2.0, 0.5) == [1.0, 0.75, 0.5, 0.25]
    assert pad_kg([0.5, 0.5, 0, 0], 1.0, -0.2) == [0.5, 0.5, 0.0, 0.0]   # negative load reading ignored
