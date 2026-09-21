"""CommandRouter: only well-formed G-code may reach the ESP32."""
import pytest

from sli_backend.core.command_router import CommandRouter


class FakeSerial:
    def __init__(self, connected=True, write_ok=True):
        self.is_connected = connected
        self.write_ok = write_ok
        self.sent = []

    def send(self, command):
        self.sent.append(command)
        return self.write_ok


VALID = [
    "T0", "RST",
    "M1 S1000 D0", "M2 S250 D1", "M3 S2500 D1", "M4 S1 D0",
    "M0 A1", "M0 A4",
    "CAL0", "CAL1", "CAL1 B0 I1 T1 Q0", "CAL1 B1", "CAL2", "CAL2 S19.0480 I1", "CAL2 I0",
    "CAL3 W2.000", "CAL3 W2", "CAL3 CLEAR",
    "DBG", "CFG RATE 50",
    "LC UPLOAD 5", "LC 15.000,0.000,1.200", "LC SAVE", "LC GET",
]

INVALID = [
    "", "   ", "hello", "M5 S1000 D0", "M1", "M1 S1000", "M1 S1000 D2", "M1 S12345 D0",
    "M0 A5", "M0", "T1", "T0 now", "RST 1",
    "CAL1 B2", "CAL1 T2", "CAL3", "CAL3 W-1", "CAL3 W1e3", "CAL3 CLEAR x", "CAL4",
    "CFG RATE", "LC", "LC UPLOAD", "LC 15,0", "LC 15,0,-1",
    "M1 S100 D0; M2 S100 D0", "M1 S100 D0\nT0",
]


@pytest.mark.parametrize("command", VALID)
def test_valid_commands_are_forwarded(command):
    serial = FakeSerial()
    ok, msg = CommandRouter(serial).send(command)
    assert ok, msg
    assert serial.sent == [command]


@pytest.mark.parametrize("command", INVALID)
def test_invalid_commands_never_reach_the_serial_port(command):
    serial = FakeSerial()
    ok, _ = CommandRouter(serial).send(command)
    assert not ok
    assert serial.sent == []


def test_whitespace_is_trimmed():
    serial = FakeSerial()
    assert CommandRouter(serial).send("  M1 S1000 D0  ")[0]
    assert serial.sent == ["M1 S1000 D0"]


def test_nothing_is_sent_when_disconnected():
    serial = FakeSerial(connected=False)
    ok, msg = CommandRouter(serial).send("M1 S1000 D0")
    assert not ok and "Not connected" in msg
    assert serial.sent == []


def test_serial_write_failure_is_reported():
    ok, msg = CommandRouter(FakeSerial(write_ok=False)).send("DBG")
    assert not ok and "failed" in msg.lower()


def test_estop_is_sent_directly_and_reports_when_disconnected():
    serial = FakeSerial()
    assert CommandRouter(serial).send_estop() == (True, "E-STOP sent")
    assert serial.sent == ["T0"]
    assert CommandRouter(FakeSerial(connected=False)).send_estop()[0] is False
