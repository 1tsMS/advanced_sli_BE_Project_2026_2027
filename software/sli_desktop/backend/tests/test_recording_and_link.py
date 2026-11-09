"""
Link handling, log download guard, encoder calibration commands.

Run from software/sli_desktop/backend:
    pip install pytest httpx
    python -m pytest -q
"""
import asyncio

import pytest
import serial
from fastapi import FastAPI
from fastapi.testclient import TestClient

from sli_backend.api import rest_endpoints
from sli_backend.api.models import TelemetryFrame
from sli_backend.core.command_router import CommandRouter
from sli_backend.core.serial_manager import SerialManager


class FakeSerial:
    """Stands in for SerialManager in the REST tests."""

    def __init__(self):
        self.connected = False
        self.sent: list[str] = []
        self.port = None
        self.baud_rate = 115200
        self.rx_rate = 0.0

    @property
    def is_connected(self):
        return self.connected

    async def connect(self, port, baud):
        self.connected, self.port = True, port
        return True

    async def disconnect(self):
        self.connected = False

    def send(self, cmd):
        self.sent.append(cmd)
        return True

    def list_ports(self):
        return []


@pytest.fixture
def api(tmp_path):
    fake = FakeSerial()
    rest_endpoints.init_endpoints(fake, CommandRouter(fake), tmp_path)
    app = FastAPI()
    app.include_router(rest_endpoints.router)
    return TestClient(app), fake, tmp_path


# ---------------------------------------------------------------- log download

def test_log_download_cannot_escape_folder(tmp_path):
    (tmp_path / "logs").mkdir()
    (tmp_path / "secret.txt").write_text("x")
    logs = tmp_path / "logs"
    assert rest_endpoints.resolve_log(logs, "../secret.txt") is None
    assert rest_endpoints.resolve_log(logs, "nope.csv") is None
    (logs / "lift_20261107_120000.csv").write_text("a")
    assert rest_endpoints.resolve_log(logs, "lift_20261107_120000.csv") is not None


def test_status_after_connect(api):
    client, fake, _ = api
    assert client.post("/api/connect", json={"port": "COM9"}).status_code == 200
    assert client.get("/api/status").json()["connected"] is True


def test_log_download_route_rejects_traversal(api):
    client, *_ = api
    assert client.get("/api/logs/..%2F..%2Fmain.py").status_code == 404


# ---------------------------------------------------------------- encoder calibration

def test_encoder_calibration_sends_direction_then_reference(api):
    client, fake, *_ = api
    fake.connected = True
    r = client.post("/api/calibrate/encoder", json={"axis": "tele", "value": 42.5, "invert": True})
    assert r.status_code == 200
    assert fake.sent == ["CAL5 A3 I1", "CAL4 A3 V42.500"]


def test_encoder_calibration_validation(api):
    client, fake, *_ = api
    fake.connected = True
    assert client.post("/api/calibrate/encoder", json={"axis": "winch", "value": 0}).status_code == 400
    assert client.post("/api/calibrate/encoder", json={"axis": "boom"}).status_code == 400
    assert client.post("/api/calibrate/encoder", json={"axis": "boom", "value": -7.25}).status_code == 200
    assert fake.sent[-1] == "CAL4 A2 V-7.250"


@pytest.mark.parametrize("cmd,ok", [
    ("CAL4 A1 V0.000", True), ("CAL4 A2 V-5.5", True), ("CAL4 A3 V120", True),
    ("CAL4 A4 V0", True), ("CAL4 A5 V0", False), ("CAL4 A1", False), ("CAL5 A1 I1", True),
    ("CAL5 A4 I1", True), ("CAL6 S0.018700", True), ("CAL6 S-1", False), ("CAL6", False),
    ("CAL5 A3 I0", True), ("CAL5 A2 I2", False), ("CAL5 A2", False),
])
def test_command_whitelist_for_encoder_cal(cmd, ok):
    fake = FakeSerial()
    fake.connected = True
    assert CommandRouter(fake).send(cmd)[0] is ok


def test_rope_calibration(api):
    client, fake, *_ = api
    fake.connected = True
    r = client.post("/api/calibrate/rope", json={"scale": 0.0187, "invert": True})
    assert r.status_code == 200
    assert fake.sent == ["CAL5 A4 I1", "CAL6 S0.018700"]
    r = client.post("/api/calibrate/rope", json={"scale": 0.02, "invert": False, "value": 312.4})
    assert r.status_code == 200
    assert fake.sent[-3:] == ["CAL5 A4 I0", "CAL6 S0.020000", "CAL4 A4 V312.400"]
    assert client.post("/api/calibrate/rope", json={"scale": 0}).status_code == 400
    assert client.post("/api/calibrate/encoder", json={"axis": "rope", "value": 0}).status_code == 200
    assert fake.sent[-1] == "CAL4 A4 V0.000"


def test_load_and_axis_scale_calibration(api):
    client, fake, *_ = api
    fake.connected = True
    assert client.post("/api/calibrate/load", json={"action": "point", "weight": 0.5}).status_code == 200
    assert fake.sent[-1] == "CAL7 W0.5000"
    assert client.post("/api/calibrate/load", json={"action": "clear"}).status_code == 200
    assert fake.sent[-1] == "CAL7 CLEAR"
    assert client.post("/api/calibrate/load", json={"action": "scale", "scale": -121500.0}).status_code == 200
    assert fake.sent[-1] == "CAL7 S-121500.00"
    assert client.post("/api/calibrate/load", json={"action": "point", "weight": 0}).status_code == 400
    assert client.post("/api/calibrate/axis-scale", json={"axis": "swing", "scale": 18, "invert": True}).status_code == 200
    assert fake.sent[-1] == "CAL8 A1 S18.0000 I1"
    assert client.post("/api/calibrate/axis-scale", json={"axis": "rope", "scale": 1}).status_code == 400


# ---------------------------------------------------------------- serial link drop

class DyingPort:
    """pyserial stand-in that delivers one line, then fails like an unplugged USB port."""

    def __init__(self):
        self.is_open = True
        self.calls = 0

    @property
    def in_waiting(self):
        return 0

    def read(self, n=1):
        self.calls += 1
        if self.calls == 1:
            return b"$ACK,ESTOP\n"
        raise serial.SerialException("ClearCommError failed (PermissionError(13, 'Access is denied.'))")

    def close(self):
        self.is_open = False


def test_reader_loop_closes_port_and_reports_drop():
    async def run():
        mgr = SerialManager()
        port = DyingPort()
        drops = []
        mgr.on_disconnect = drops.append
        mgr._serial, mgr._connected = port, True
        await asyncio.wait_for(mgr._reader_loop(), timeout=2)
        return mgr, port, drops

    mgr, port, drops = asyncio.run(run())
    assert mgr.rx_queue.get_nowait() == "$ACK,ESTOP"
    assert not mgr.is_connected
    assert port.is_open is False
    assert len(drops) == 1 and "PermissionError" in drops[0]
