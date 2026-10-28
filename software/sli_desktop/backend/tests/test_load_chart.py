"""Load chart profiles, upload lines, $S / $LC parsing."""
from sli_backend.api.models import LoadChartProfile
from sli_backend.core import load_chart
from sli_backend.core.command_router import CommandRouter
from sli_backend.core.packet_parser import PacketParser


def profile(**kw) -> LoadChartProfile:
    base = dict(
        name="Standard 85",
        anglesDeg=[0, 10, 20],
        extensionsMM=[22.4, 67.4],
        capacityKg=[[0.6, 0.42], [0.6, 0.42], [0.65, 0.45]],
        zones=[{"fromDeg": 135, "toDeg": 225, "factor": 0.5}],
    )
    base.update(kw)
    return LoadChartProfile(**base)


def test_valid_profile_passes():
    assert load_chart.validate(profile()) is None


def test_validation_mirrors_firmware():
    bad = [
        profile(name=""), profile(name="x" * 16), profile(name="Café"), profile(name=" pad"),
        profile(anglesDeg=[0], capacityKg=[[0.6, 0.42]]),
        profile(anglesDeg=[0, 0, 20]),
        profile(extensionsMM=[67.4, 22.4]),
        profile(capacityKg=[[0.6, 0.42], [0.6, 0.0], [0.65, 0.45]]),       # zero cell
        profile(capacityKg=[[0.6, 0.42], [0.6, 11.0], [0.65, 0.45]]),      # > 10 kg
        profile(capacityKg=[[0.6, 0.42], [0.6, 0.42]]),                    # missing row
        profile(zones=[{"fromDeg": 10, "toDeg": 10, "factor": 0.5}]),
        profile(zones=[{"fromDeg": 10, "toDeg": 20, "factor": 2.0}]),
        profile(warnPct=100, cutPct=100),
        profile(cutPct=200),
    ]
    for p in bad:
        assert load_chart.validate(p) is not None, p


def test_upload_lines_pass_the_router_whitelist():
    router = CommandRouter.__new__(CommandRouter)
    cmds = load_chart.to_commands(profile())
    assert cmds[0] == "LC BEGIN 3 2 Standard 85"
    assert cmds[1] == "LC A 0 10 20"
    assert cmds[2] == "LC E 22.4 67.4"
    assert cmds[3] == "LC R 0 0.6 0.42"
    assert "LC Z 135 225 0.5" in cmds
    assert cmds[-2] == "LC P 90 100" and cmds[-1] == "LC COMMIT"
    for c in cmds:
        assert router._is_valid(c), c
        assert len(c) < 128          # firmware line buffer


def test_largest_chart_fits_the_line_buffer():
    p = profile(anglesDeg=[-10, 0, 10, 20, 30, 40, 50, 60],
                extensionsMM=[22.4, 31.123, 40.5, 49.25, 58.75, 67.4],
                capacityKg=[[9.999] * 6 for _ in range(8)],
                zones=[{"fromDeg": 315.5, "toDeg": 45.5, "factor": 0.75}] * 4)
    assert load_chart.validate(p) is None
    router = CommandRouter.__new__(CommandRouter)
    for c in load_chart.to_commands(p):
        assert len(c) < 128 and router._is_valid(c), c


def test_store_roundtrip(tmp_path):
    store = load_chart.ProfileStore(tmp_path)
    saved = store.save(profile())
    assert saved.updated
    assert (tmp_path / "Standard_85.json").exists()
    assert store.get("Standard 85").capacityKg == profile().capacityKg
    assert [p.name for p in store.list()] == ["Standard 85"]
    (tmp_path / "broken.json").write_text("{not json", encoding="utf-8")
    assert len(store.list()) == 1                  # a broken file doesn't hide the rest
    assert store.delete("Standard 85") and not store.delete("Standard 85")


def test_parse_safety_line():
    s = PacketParser.parse_safety("$S,CUT,101.5,0.590,-1,0,5D,Standard 85")
    assert s.state == "CUT" and s.pct == 101.5 and s.capKg == 0.59
    assert s.zone == -1 and s.blocked == 0x5D and s.chart == "Standard 85"
    assert s.mode == "ARMED"                        # old 8-field firmware, no bypass
    assert PacketParser.parse_safety("$S,OK,1,2,-1,120,00,X").mode == "BYPASS"
    off = PacketParser.parse_safety("$S,CUT,101.5,0.590,-1,0,5D,Standard 85,OFF")
    assert off.mode == "OFF" and off.blocked == 0x5D
    assert PacketParser.parse_safety("$S,OK,1,2") is None
    assert PacketParser.parse_safety("$S,OK,x,0.5,-1,0,00,A") is None


def test_chart_readback_assembles():
    got = []
    p = PacketParser()
    p.set_callbacks(on_chart=got.append)
    for line in ["$LC,NAME,Standard 85", "$LC,A,0.000,10.000,20.000", "$LC,E,22.400,67.400",
                 "$LC,R,0,0.600,0.420", "$LC,R,1,0.600,0.420", "$LC,R,2,0.650,0.450",
                 "$LC,Z,135.0,225.0,0.500", "$LC,P,90.0,100.0", "$LC,END"]:
        p.parse_line(line)
    assert len(got) == 1
    c = got[0]
    assert c["name"] == "Standard 85" and c["anglesDeg"] == [0, 10, 20]
    assert c["capacityKg"][2] == [0.65, 0.45] and c["zones"][0]["factor"] == 0.5
    assert load_chart.validate(LoadChartProfile(**c)) is None

    p.parse_line("$LC,NONE")
    assert got[-1] == {"none": True}
