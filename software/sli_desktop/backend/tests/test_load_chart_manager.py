"""LoadChartManager against a simulated ESP32 that follows the firmware's LC protocol."""
import asyncio

import pytest

import sli_backend.core.load_chart as load_chart_module
from sli_backend.api.models import LoadChartEntry
from sli_backend.core.load_chart import LoadChartError, LoadChartManager

ROWS = [(15, 0, 1.2), (30, 0, 2.5), (45, 0, 3.8), (30, 100, 1.8)]


def entries(rows=ROWS):
    return [LoadChartEntry(angle=a, extensionMM=e, limitKg=l) for a, e, l in rows]


class FakeEsp:
    """Mimics firmware load_chart.cpp + the command task's LC handler."""

    def __init__(self, behaviour="ok"):
        self.behaviour = behaviour          # ok | reject_entry | silent | no_save_reply
        self.flash = []
        self.uploading = False
        self.expected = 0
        self.staging = []
        self.manager = None
        self.connected = True

    def emit(self, line):
        self.manager.handle_line(line)

    # --- router interface ---
    def send(self, command):
        if not self.connected:
            return False, "Not connected to ESP32"
        self.receive(command)
        return True, "OK"

    def receive(self, cmd):
        if self.behaviour == "silent":
            return
        if cmd.startswith("LC UPLOAD"):
            n = int(cmd.split()[2])
            if n == 0 or n > 32:
                self.emit("$LC,ERR,BAD_COUNT")
            else:
                self.uploading, self.expected, self.staging = True, n, []
                self.emit(f"$LC,READY,{n}")
        elif cmd == "LC SAVE":
            if self.behaviour == "no_save_reply":
                return
            if len(self.staging) != self.expected:
                self.emit("$LC,ERR,INCOMPLETE")
            else:
                self.flash, self.uploading = list(self.staging), False
                self.emit(f"$LC,OK,{len(self.flash)}")
        elif cmd == "LC GET":
            self.emit(f"$LC,BEGIN,{len(self.flash)}")
            for i, (a, e, l) in enumerate(self.flash):
                self.emit(f"$LC,E,{i},{a:.2f},{e:.1f},{l:.2f}")
            self.emit("$LC,END")
        elif cmd.startswith("LC "):
            a, e, l = (float(x) for x in cmd[3:].split(","))
            if self.behaviour == "reject_entry" and l > 3.0:
                self.emit("$LC,ERR,RANGE")
            else:
                self.staging.append((a, e, l))


def run(coro_factory, behaviour="ok"):
    """Build manager + fake ESP inside the event loop, then run the coroutine."""
    async def go():
        esp = FakeEsp(behaviour)
        manager = LoadChartManager(esp)
        esp.manager = manager
        return await coro_factory(manager, esp)
    return asyncio.run(go())


@pytest.fixture(autouse=True)
def fast_timeouts(monkeypatch):
    monkeypatch.setattr(load_chart_module, "_REPLY_TIMEOUT_S", 0.2)
    monkeypatch.setattr(load_chart_module, "_SAVE_TIMEOUT_S", 0.2)
    monkeypatch.setattr(load_chart_module, "_ENTRY_GAP_S", 0.0)


def test_upload_saves_every_row_and_confirms_the_count():
    async def scenario(manager, esp):
        saved = await manager.upload(entries())
        return saved, esp.flash
    saved, flash = run(scenario)
    assert saved == 4
    assert flash == ROWS


def test_read_back_returns_what_is_stored():
    async def scenario(manager, esp):
        await manager.upload(entries())
        return await manager.read()
    chart = run(scenario)
    assert [(e.angle, e.extensionMM, e.limitKg) for e in chart] == ROWS


def test_read_of_an_empty_chart_is_an_empty_list():
    async def scenario(manager, esp):
        return await manager.read()
    assert run(scenario) == []


def test_esp_rejecting_an_entry_fails_the_upload_and_saves_nothing():
    async def scenario(manager, esp):
        with pytest.raises(LoadChartError, match="RANGE"):
            await manager.upload(entries())
        return esp.flash
    assert run(scenario, "reject_entry") == []


def test_no_reply_from_the_esp_is_reported_not_assumed_successful():
    async def scenario(manager, esp):
        with pytest.raises(LoadChartError, match="READY"):
            await manager.upload(entries())
    run(scenario, "silent")


def test_missing_save_confirmation_is_an_error():
    async def scenario(manager, esp):
        with pytest.raises(LoadChartError, match="OK"):
            await manager.upload(entries())
    run(scenario, "no_save_reply")


def test_not_connected_is_reported():
    async def scenario(manager, esp):
        esp.connected = False
        with pytest.raises(LoadChartError, match="Not connected"):
            await manager.upload(entries())
        with pytest.raises(LoadChartError, match="Not connected"):
            await manager.read()
    run(scenario)


def test_a_failed_upload_does_not_poison_the_next_one():
    async def scenario(manager, esp):
        esp.behaviour = "reject_entry"
        with pytest.raises(LoadChartError):
            await manager.upload(entries())
        esp.behaviour = "ok"
        return await manager.upload(entries())
    assert run(scenario) == 4


def test_stale_replies_from_earlier_requests_are_ignored():
    async def scenario(manager, esp):
        manager.handle_line("$LC,OK,99")       # left over from some earlier request
        return await manager.upload(entries())
    assert run(scenario) == 4
