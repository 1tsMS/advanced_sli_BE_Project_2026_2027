"""SessionLogger CSV output and the REST API (with no ESP32 connected)."""
import csv

import pytest
from fastapi.testclient import TestClient

import sli_backend.core.session_logger as session_logger_module
from sli_backend.api.models import TelemetryFrame
from sli_backend.core.session_logger import SessionLogger


# ---------------------------------------------------------------- session logger

@pytest.fixture
def logger_in_tmp(tmp_path, monkeypatch):
    monkeypatch.setattr(session_logger_module, "LOG_DIR", tmp_path)
    return SessionLogger()


def test_csv_has_the_current_columns_and_values(logger_in_tmp):
    lg = logger_in_tmp
    path = lg.start_session()
    lg.log_frame(TelemetryFrame(
        boomAngle=45.5, measuredLoad=1.4, actualLoad=2.0, boomLean=-2.5,
        statusFlags=33, safeLoadLimit=5.0, loadPercent=40.0, alarmLevel=1, fsr=[1, 2, 3, 4],
        timestamp=1_700_000_000.0,
    ))
    lg.end_session()

    rows = list(csv.DictReader(open(path, encoding="utf-8")))
    assert len(rows) == 1
    row = rows[0]
    assert row["boomLean"] == "-2.5" and row["statusFlags"] == "33"
    assert row["measuredLoad"] == "1.4" and row["actualLoad"] == "2.0"
    assert row["fsr1"] == "1" and row["fsr4"] == "4"
    assert "imuRoll" not in row and "imuPitch" not in row


def test_session_listing_shows_files_but_not_local_paths(logger_in_tmp):
    lg = logger_in_tmp
    lg.start_session()
    lg.end_session()
    sessions = lg.list_sessions()
    assert len(sessions) == 1
    assert set(sessions[0]) == {"filename", "size", "modified"}


def test_logging_without_a_session_is_a_no_op(logger_in_tmp):
    logger_in_tmp.log_frame(TelemetryFrame())     # must not raise
    assert logger_in_tmp.list_sessions() == []


# ---------------------------------------------------------------------- REST API
# One TestClient (one event loop) for the whole module: the app's serial queue is
# bound to the loop it first runs in.

@pytest.fixture(scope="module")
def client():
    import main
    with TestClient(main.app) as c:
        yield c


def test_status_reports_disconnected(client):
    body = client.get("/api/status").json()
    assert body["connected"] is False and body["backendOK"] is True


def test_ports_endpoint_answers(client):
    assert "ports" in client.get("/api/ports").json()


def test_commands_are_refused_when_not_connected(client):
    r = client.post("/api/command", json={"command": "M1 S1000 D0"})
    assert r.status_code == 400 and "Not connected" in r.json()["detail"]


def test_malformed_commands_are_refused(client):
    assert client.post("/api/command", json={"command": "rm -rf /"}).status_code == 400


def test_estop_reports_failure_when_not_connected(client):
    body = client.post("/api/estop").json()
    assert body["ok"] is False


def test_load_chart_validation(client):
    row = {"angle": 30, "extensionMM": 0, "limitKg": 2.5}
    assert client.post("/api/loadchart/upload", json={"entries": []}).status_code == 422
    assert client.post("/api/loadchart/upload", json={"entries": [dict(row, angle=95)]}).status_code == 422
    assert client.post("/api/loadchart/upload", json={"entries": [dict(row, limitKg=-1)]}).status_code == 422
    assert client.post("/api/loadchart/upload", json={"entries": [row] * 33}).status_code == 422
    dup = client.post("/api/loadchart/upload", json={"entries": [row, row]})
    assert dup.status_code == 400 and "Duplicate" in dup.json()["detail"]


def test_valid_load_chart_upload_fails_cleanly_without_an_esp32(client):
    r = client.post("/api/loadchart/upload",
                    json={"entries": [{"angle": 30, "extensionMM": 0, "limitKg": 2.5}]})
    assert r.status_code == 502 and "Not connected" in r.json()["detail"]


def test_load_gain_validation(client):
    assert client.post("/api/calibrate/loadgain", json={"weightKg": 0.01}).status_code == 422
    assert client.post("/api/calibrate/loadgain", json={"weightKg": 500}).status_code == 422
    ok = client.post("/api/calibrate/loadgain", json={"weightKg": 2}).json()
    assert ok["ok"] is False        # valid request, but nothing is connected


def test_log_download_of_a_missing_file_is_404(client):
    assert client.get("/api/logs/does_not_exist.csv").status_code == 404
