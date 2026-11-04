"""
Lift analysis, tipping margin, lift recorder and the lift / shaping REST routes.

Run from software/sli_desktop/backend:
    python -m pytest -q
"""
import math

import pytest
from fastapi import FastAPI
from fastapi.testclient import TestClient

from sli_backend.api import lift_endpoints
from sli_backend.api.models import TelemetryFrame, SafetyStatus
from sli_backend.core import tipping
from sli_backend.core.lift_analysis import analyse, downsample, AnalysisSettings
from sli_backend.core.lift_recorder import LiftRecorder, MotionTracker


# ---------------- synthetic lift ----------------
def make_lift(snatch=False, warn=False, ripple_g=0.0, rate=50.0):
    """0-2 s idle, 2-4 s winch up (load comes on at 3 s), 5-8 s slew,
    8-11 s hold (load ripple after the slew stop), 11-13 s winch down, set-down at 12 s."""
    rows, kg = [], 0.8
    for i in range(int(14 * rate)):
        t = i / rate
        m4 = -1 if 2 <= t < 4 else (1 if 11 <= t < 13 else 0)
        m1 = 1 if 5 <= t < 8 else 0
        if t < 3 or t >= 12:
            load = 0.0
        elif t < 3.3:
            load = kg * (t - 3) / 0.3
        else:
            load = kg
        if snatch and 3.3 <= t < 3.6:
            load = kg * 1.6
        if ripple_g and 8 <= t < 11:
            load += ripple_g / 1000 * math.sin(2 * math.pi * 1.2 * t)
        state = "WARN" if warn and 6 <= t < 7 else "OK"
        rows.append({"t": t, "load": load, "pct": 100 * load / 1.0, "capKg": 1.0, "state": state,
                     "mode": "ARMED", "m1": m1, "m2": 0, "m3": 0, "m4": m4, "shaping": 0,
                     "tipValid": 1, "tipMargin": 80.0})
    return rows


def test_pickup_and_phases():
    a = analyse(make_lift())
    assert a["ok"]
    assert len(a["pickups"]) == 1
    p = a["pickups"][0]
    assert abs(p["staticKg"] - 0.8) < 0.01 and p["daf"] < 1.05 and not p["snatch"]
    names = [ph["name"] for ph in a["phases"]]
    for n in ("idle", "approach", "pick-up", "slew", "hold", "set-down"):
        assert n in names, n
    assert a["load"]["liftedKg"] == pytest.approx(0.8, abs=0.01)


def test_snatch_is_flagged_and_costs_points():
    calm, jerk = analyse(make_lift()), analyse(make_lift(snatch=True))
    assert jerk["pickups"][0]["snatch"] and jerk["smoothness"]["dafMax"] == pytest.approx(1.6, abs=0.01)
    assert jerk["score"]["smoothness"] < calm["score"]["smoothness"]
    assert jerk["penalty"]["points"] == calm["penalty"]["points"] + AnalysisSettings().pen["snatch"]
    assert any("Snatch" in f["text"] for f in jerk["flags"])


def test_warn_penalty_and_cost_formula():
    a = analyse(make_lift(warn=True))
    assert a["safety"]["warnEntries"] == 1 and a["safety"]["warnS"] == pytest.approx(1.0, abs=0.05)
    cfg = AnalysisSettings()
    pts = a["penalty"]["points"]
    assert pts == pytest.approx(cfg.pen["warnEvent"] + cfg.pen["warnPerS"] * 1.0, abs=0.1)
    c = a["cost"]
    assert c["time"] == pytest.approx(a["durationS"] / 60 * cfg.ratePerMin, abs=0.01)
    assert c["load"] == pytest.approx(0.8 * cfg.ratePerKg, abs=1)
    assert c["total"] == pytest.approx(c["time"] + c["load"] + c["penalty"], abs=0.01)


def test_ripple_after_stop():
    a = analyse(make_lift(ripple_g=30))
    swing = [s for s in a["smoothness"]["stops"] if s["axis"] == "swing"]
    assert swing and swing[0]["rippleG"] == pytest.approx(30 / math.sqrt(2), rel=0.15)
    assert analyse(make_lift())["smoothness"]["meanRippleG"] < 1


def test_bypass_and_empty():
    rows = make_lift()
    for r in rows:
        r["mode"] = "OFF"
    a = analyse(rows)
    assert any(i["what"] == "SLI not armed" for i in a["penalty"]["items"])
    assert not analyse(rows[:1])["ok"]
    empty = [{**r, "load": 0.0} for r in make_lift()]
    e = analyse(empty)
    assert e["ok"] and e["pickups"] == [] and any("No load" in f["text"] for f in e["flags"])


def test_score_bounds():
    for kw in ({}, {"snatch": True}, {"warn": True}, {"ripple_g": 80}):
        s = analyse(make_lift(**kw))["score"]
        assert 0 <= s["total"] <= 100 and s["grade"] in "ABCD"


def test_downsample_keeps_peak():
    rows = make_lift(snatch=True)
    ds = downsample(rows, 100)
    assert len(ds) == 100 and ds[0] is rows[0] and ds[-1] is rows[-1]
    assert max(r["load"] for r in ds) == pytest.approx(1.28, abs=0.01)


# ---------------- tipping ----------------
def test_tipping_margin():
    cfg = tipping.TippingConfig()
    even = tipping.compute([2000, 2000, 2000, 2000], cfg)
    assert even["valid"] and even["marginPct"] == pytest.approx(100.0, abs=0.1)
    # all weight on the two pads of the +x edge -> CoP on that edge -> 0 %
    edge = tipping.compute([0, 2000, 2000, 0], cfg)
    assert edge["marginPct"] == pytest.approx(0.0, abs=0.1) and edge["warn"]
    # leaning toward +x
    lean = tipping.compute([1000, 3000, 3000, 1000], cfg)
    assert 0 < lean["marginPct"] < 100 and lean["copX"] > 0 and abs(lean["copY"]) < 1e-6
    assert not tipping.compute([0, 0, 0, 0], cfg)["valid"]


def test_tipping_winding_and_calibration():
    cw = tipping.TippingConfig(legs=[[-100, 100], [100, 100], [100, -100], [-100, -100]])
    assert tipping.compute([2000] * 4, cw)["marginPct"] == pytest.approx(100.0, abs=0.1)
    gains = tipping.calibration_gains([[1000, 2000, 2000, 2000]] * 10)
    cfg = tipping.TippingConfig(gains=gains)
    assert tipping.compute([1000, 2000, 2000, 2000], cfg)["marginPct"] == pytest.approx(100.0, abs=0.5)
    assert tipping.calibration_gains([[0, 2000, 2000, 2000]]) is None


# ---------------- recorder ----------------
def test_motion_tracker():
    m = MotionTracker()
    m.on_command("M1 S800 D1", 10.0)
    m.on_command("M4 S200 D0", 10.0)
    assert m.state(10.1) == {1: 1, 2: 0, 3: 0, 4: -1}
    assert m.state(10.5)[1] == 0                       # dead-man: no repeat for > 0.4 s
    m.on_command("M1 S800 D0", 11.0)
    m.on_command("M0 A1", 11.1)
    assert m.state(11.15)[1] == 0
    m.on_command("M2 S500 D1", 12.0)
    m.on_command("T0", 12.05)
    assert m.state(12.1)[2] == 0


def _frame(ts, load, fsr=(2000, 2000, 2000, 2000)):
    return TelemetryFrame(actualLoad=load, measuredLoad=load, safeLoadLimit=1.0, loadPercent=100 * load,
                          fsr=list(fsr), timestamp=ts)


def test_recorder_roundtrip(tmp_path):
    rec = LiftRecorder(tmp_path / "logs", tmp_path / "cfg")
    assert rec.operators() == ["MS", "Ali", "Aayush"]
    assert rec.add_operator("Guest 1")[-1] == "Guest 1"
    with pytest.raises(ValueError):
        rec.add_operator("../evil")
    rec.start("MS", "test")
    rec.on_safety(SafetyStatus(state="OK", mode="ARMED"))
    cfg = tipping.TippingConfig()
    t0 = rec._t0
    for i in range(400):
        ts = t0 + i / 50
        if i == 50:
            rec.on_command("M4 S200 D0")
        load = 0.5 if i > 100 else 0.0
        rec.on_frame(_frame(ts, load), tipping.compute([2000] * 4, cfg))
    rec.on_ack("$ACK,SLI,REFUSED,A2,D0,CUT")
    rec.on_ack("$ACK,SLI,REFUSED,A2,D0,CUT")      # same episode (button still held)
    report = rec.stop()
    assert report["safety"]["refusals"] == 1
    assert report["ok"] and report["operator"] == "MS" and report["pickups"]
    assert report["events"][0]["kind"] == "refused"
    lifts = rec.list_lifts()
    assert len(lifts) == 1 and lifts[0]["id"] == report["id"]
    rows = rec.rows(report["id"])
    assert len(rows) == 400 and rows[-1]["load"] == 0.5
    rec.save_settings({"ratePerMin": 10, "pen": {"refusal": 50}})
    again = rec.reanalyse(report["id"])
    assert again["cost"]["rates"]["perMin"] == 10
    assert any(i["what"] == "Motions refused by SLI" and i["points"] == 50 for i in again["penalty"]["items"])
    assert rec.report("../../etc") is None
    assert rec.delete(report["id"]) and rec.list_lifts() == []


# ---------------- REST ----------------
class FakeRouter:
    def __init__(self):
        self.sent = []

    def send(self, cmd):
        self.sent.append(cmd)
        return True, "OK"


@pytest.fixture
def client(tmp_path):
    rec = LiftRecorder(tmp_path / "logs", tmp_path / "cfg")
    store = tipping.TippingStore(tmp_path / "cfg" / "tipping.json")
    (tmp_path / "cfg").mkdir(exist_ok=True)
    router = FakeRouter()
    lift_endpoints.init_lift_endpoints(rec, store, router)
    app = FastAPI()
    app.include_router(lift_endpoints.router)
    c = TestClient(app)
    c.rec, c.router, c.store = rec, router, store
    return c


def test_rest_lift_flow(client):
    assert client.post("/api/lift/stop").status_code == 409
    assert client.post("/api/lift/start", json={"operator": " "}).status_code == 400
    assert client.post("/api/lift/start", json={"operator": "Ali"}).json()["active"]
    t0 = client.rec._t0
    for i in range(200):
        client.rec.on_frame(_frame(t0 + i / 50, 0.3 if i > 40 else 0.0), {"valid": False})
    rep = client.post("/api/lift/stop").json()
    assert rep["operator"] == "Ali"
    lid = rep["id"]
    assert client.get("/api/lifts").json()["lifts"][0]["id"] == lid
    s = client.get(f"/api/lifts/{lid}/series?n=50").json()
    assert s["total"] == 200 and len(s["rows"]) == 50
    assert client.get("/api/lifts/lift_00000000_000000").status_code == 404
    assert client.post("/api/operators", json={"name": "Prof N"}).json()["operators"][-1] == "Prof N"


def test_rest_shaping_and_tipping(client):
    client.post("/api/shaping", json={"on": True})
    client.post("/api/shaping", json={"on": True, "lengthMm": 260})
    client.post("/api/shaping", json={"on": False})
    assert client.router.sent == ["SHP 1", "SHP 1 L260", "SHP 0"]
    assert client.post("/api/tipping/calibrate").status_code == 409     # no FSR samples yet
    for _ in range(20):
        client.store.update([1000, 2000, 2000, 2000])
    g = client.post("/api/tipping/calibrate").json()
    assert g["calibrated"] and g["gains"][0] > 1.5
    bad = client.put("/api/tipping", json={**g, "legs": [[0, 0]]})
    assert bad.status_code == 400
