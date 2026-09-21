"""PacketParser: turns the ESP32's text lines into typed frames and reports."""
from sli_backend.core.packet_parser import PacketParser

# $T,boomAngle,extensionMM,measuredLoad,actualLoad,swingAngle,ropeLen,fsr1..4,boomLean,statusFlags,safeLimit,loadPct,alarm
GOOD_T = "$T,45.50,120.0,1.40,2.00,10.00,300.0,100,200,300,400,-2.50,33,5.00,40.0,1"


def make_parser():
    parser = PacketParser()
    got = {"t": [], "d": [], "ack": [], "lc": []}
    parser.set_callbacks(
        on_telemetry=got["t"].append,
        on_debug=got["d"].append,
        on_ack=got["ack"].append,
        on_loadchart=got["lc"].append,
    )
    return parser, got


def test_telemetry_frame_is_parsed_field_by_field():
    parser, got = make_parser()
    parser.parse_line(GOOD_T)
    assert len(got["t"]) == 1
    f = got["t"][0]
    assert f.boomAngle == 45.5
    assert f.extensionMM == 120.0
    assert f.measuredLoad == 1.4      # uncorrected kg
    assert f.actualLoad == 2.0        # angle-corrected kg
    assert f.swingAngle == 10.0
    assert f.ropeLength == 300.0
    assert f.fsr == [100, 200, 300, 400]
    assert f.boomLean == -2.5
    assert f.statusFlags == 33
    assert f.safeLoadLimit == 5.0
    assert f.loadPercent == 40.0
    assert f.alarmLevel == 1
    assert f.timestamp > 0


def test_wrong_field_count_is_dropped():
    parser, got = make_parser()
    parser.parse_line("$T,1,2,3")
    parser.parse_line(GOOD_T + ",99")
    assert got["t"] == []


def test_packet_from_old_firmware_is_rejected_not_misread():
    # Old layout had imuRoll/imuPitch decimals where boomLean/statusFlags are now
    old = "$T,45.50,120.0,1.40,2.00,10.00,300.0,100,200,300,400,45.50,-2.50,5.00,40.0,1"
    parser, got = make_parser()
    parser.parse_line(old)
    assert got["t"] == []


def test_non_numeric_field_is_dropped():
    parser, got = make_parser()
    parser.parse_line(GOOD_T.replace("45.50", "abc", 1))
    assert got["t"] == []


def test_debug_report_is_collected_until_end():
    parser, got = make_parser()
    for line in [
        "$D,I2C0_SWING,0x36,OK",
        "$D,HX711,NA,FAIL",
        "$D,FSR1,3412",
        "$D,N20_TICKS,120",
        "$D,TELE_SCALE,19.0480",
        "$D,TELE_INV,1",
        "$D,BOOM_XCHECK,1.5,OK",
        "$D,GAIN,30.0,0.7000",
    ]:
        parser.parse_line(line)
    assert got["d"] == []                      # nothing until $D,END
    parser.parse_line("$D,END")
    assert len(got["d"]) == 1
    entries = {e.bus: e for e in got["d"][0].entries}
    assert entries["I2C0_SWING"].address == "0x36" and entries["I2C0_SWING"].status == "OK"
    assert entries["HX711"].status == "FAIL"
    assert entries["FSR1"].status == "3412" and entries["FSR1"].address is None
    assert entries["TELE_SCALE"].status == "19.0480"
    assert entries["BOOM_XCHECK"].address == "1.5" and entries["BOOM_XCHECK"].status == "OK"
    assert entries["GAIN"].address == "30.0" and entries["GAIN"].status == "0.7000"


def test_debug_reports_do_not_leak_into_each_other():
    parser, got = make_parser()
    parser.parse_line("$D,FSR1,1")
    parser.parse_line("$D,END")
    parser.parse_line("$D,FSR2,2")
    parser.parse_line("$D,END")
    assert [len(r.entries) for r in got["d"]] == [1, 1]


def test_ack_and_load_chart_lines_are_routed():
    parser, got = make_parser()
    parser.parse_line("$ACK,CAL0,TARE_DONE")
    parser.parse_line("$LC,READY,5")
    parser.parse_line("$LC,ERR,RANGE")
    assert got["ack"] == ["$ACK,CAL0,TARE_DONE"]
    assert got["lc"] == ["$LC,READY,5", "$LC,ERR,RANGE"]


def test_boot_text_and_blank_lines_are_ignored():
    parser, got = make_parser()
    parser.parse_line("")
    parser.parse_line("=== Advanced SLI ESP32 Starting ===")
    parser.parse_line("AS5600 Swing (21/22): DETECTED")
    assert all(not v for v in got.values())
