// Host-side unit tests for the safety-related firmware logic.
// They compile the REAL firmware sources from ../esp32_main against the stand-in
// Arduino headers in ./stubs, so they run on a PC with no ESP32 attached.
// Run with:  python run_tests.py
#include "Arduino.h"
#include "Preferences.h"
#include "config.h"
#include "gcode_parser.h"
#include "load_chart.h"
#include "load_comp.h"
#include "safety_eval.h"
#include "telemetry_fmt.h"
#include "boom_check.h"
#include <string>

uint32_t g_fake_millis = 0;
bool g_mutex_available = true;
HardwareSerial Serial;
SemaphoreHandle_t sensorMutex = 1;
volatile bool g_estopLatched = false;

static int g_pass = 0, g_fail = 0;
#define CHECK(cond) do { if (cond) { g_pass++; } else { g_fail++; printf("  FAIL line %d: %s\n", __LINE__, #cond); } } while (0)
#define NEAR(a, b) CHECK(fabsf((float)(a) - (float)(b)) < 0.01f)
#define SECTION(name) printf("== %s\n", name)

static void resetFlash() { Preferences::store().clear(); Preferences::failWrites() = false; g_mutex_available = true; }

// Upload a chart through the same calls the command task makes
static LoadChartStatus uploadChart(LoadChart& c, const LoadChartEntry* rows, int n) {
    LoadChartStatus s = c.beginUpload(n);
    if (s != LC_OK) return s;
    for (int i = 0; i < n; i++) { s = c.addEntry(rows[i].angle, rows[i].extensionMM, rows[i].limitKg); if (s != LC_OK) return s; }
    return c.commit();
}

static const LoadChartEntry DEMO[] = {
    {15,0,1.2f},{30,0,2.5f},{45,0,3.8f},{60,0,4.5f},{75,0,5.0f},{30,100,1.8f}
};

int main() {
    // ------------------------------------------------------------------ parser
    SECTION("parser");
    {
        GCodeParser p; ParsedCommand c;
        CHECK(p.parse("CAL3 W2.5", &c) == PARSE_OK && c.type == CMD_CALIBRATE && c.calType == CAL_LOAD_GAIN);
        NEAR(c.calWeight, 2.5f); CHECK(!c.calClear);
        CHECK(p.parse("CAL3 CLEAR", &c) == PARSE_OK && c.calClear);
        CHECK(p.parse("CAL3", &c) == PARSE_INVALID_PARAMS);
        CHECK(p.parse("CAL3 W0", &c) == PARSE_INVALID_PARAMS);
        CHECK(p.parse("RST", &c) == PARSE_OK && c.type == CMD_RESET);
        CHECK(p.parse("T0", &c) == PARSE_OK && c.type == CMD_ESTOP);
        CHECK(p.parse("M4 S2500 D1", &c) == PARSE_OK && c.motor.speed == 2500 && c.motor.direction == 1);
        CHECK(p.parse("M1 S9999 D0", &c) == PARSE_OK && c.motor.speed == MOTOR_SPEED_MAX);
        CHECK(p.parse("LC UPLOAD 5", &c) == PARSE_OK && c.type == CMD_LOADCHART && c.lcOp == LC_OP_UPLOAD && c.lcCount == 5);
        CHECK(p.parse("LC 15.500,0.000,1.200", &c) == PARSE_OK && c.lcOp == LC_OP_ENTRY);
        NEAR(c.lcAngle, 15.5f); NEAR(c.lcExtension, 0); NEAR(c.lcLimit, 1.2f);
        CHECK(p.parse("LC SAVE", &c) == PARSE_OK && c.lcOp == LC_OP_SAVE);
        CHECK(p.parse("LC GET", &c) == PARSE_OK && c.lcOp == LC_OP_GET);
        CHECK(p.parse("LC 15,0", &c) == PARSE_OK && c.lcOp == LC_OP_INVALID);
        CHECK(p.parse("LC garbage", &c) == PARSE_OK && c.lcOp == LC_OP_INVALID);
        CHECK(p.parse("LCX", &c) != PARSE_OK || c.type != CMD_LOADCHART);
    }

    // -------------------------------------------------------------- load chart
    SECTION("load chart: upload state machine + flash");
    {
        resetFlash();
        LoadChart c;
        CHECK(!c.hasChart());
        CHECK(c.beginUpload(0) == LC_ERR_BAD_COUNT);
        CHECK(c.beginUpload(33) == LC_ERR_BAD_COUNT);
        CHECK(c.addEntry(10, 0, 1) == LC_ERR_NOT_UPLOADING);
        CHECK(c.commit() == LC_ERR_NOT_UPLOADING);

        CHECK(c.beginUpload(2) == LC_OK);
        CHECK(c.addEntry(91, 0, 1) == LC_ERR_RANGE);
        CHECK(c.addEntry(10, -1, 1) == LC_ERR_RANGE);
        CHECK(c.addEntry(10, 0, 101) == LC_ERR_RANGE);
        CHECK(c.addEntry(NAN, 0, 1) == LC_ERR_RANGE);
        CHECK(c.addEntry(10, 0, 1) == LC_OK);
        CHECK(c.commit() == LC_ERR_INCOMPLETE);          // 1 of 2 so far
        CHECK(c.addEntry(20, 0, 2) == LC_OK);
        CHECK(c.addEntry(30, 0, 3) == LC_ERR_OVERFLOW);
        CHECK(!c.hasChart());                             // nothing active until SAVE
        CHECK(c.commit() == LC_OK);
        CHECK(c.count() == 2 && c.hasChart());

        // Power cycle: a fresh object loads it back from "flash"
        LoadChart c2; c2.loadFromFlash();
        CHECK(c2.count() == 2);
        LoadChartEntry e; CHECK(c2.get(1, &e)); NEAR(e.angle, 20); NEAR(e.limitKg, 2);

        // Flash failure leaves the active chart untouched
        Preferences::failWrites() = true;
        CHECK(uploadChart(c, DEMO, 6) == LC_ERR_FLASH);
        CHECK(c.count() == 2);
        Preferences::failWrites() = false;

        // Mutex busy: flash written, active kept, retrying SAVE succeeds
        g_mutex_available = false;
        CHECK(uploadChart(c, DEMO, 6) == LC_ERR_BUSY);
        CHECK(c.count() == 2);
        g_mutex_available = true;
        CHECK(c.commit() == LC_OK);
        CHECK(c.count() == 6);

        // Corrupt record in flash (wrong byte length) is ignored, not trusted
        Preferences::store()["sli_lc"]["e"] = std::vector<uint8_t>(5, 0xAA);
        LoadChart c3; c3.loadFromFlash(); CHECK(c3.count() == 0);
    }

    SECTION("load chart: conservative lookup");
    {
        resetFlash();
        LoadChart c; CHECK(uploadChart(c, DEMO, 6) == LC_OK);
        NEAR(c.lookupLimit(45, 0),   3.8f);   // best of the rows that are never safer
        NEAR(c.lookupLimit(50, 0),   3.8f);   // between rows: rounds DOWN to the 45 deg row
        NEAR(c.lookupLimit(80, 0),   5.0f);
        NEAR(c.lookupLimit(45, 100), 1.8f);   // only the 100 mm row reaches this far out
        NEAR(c.lookupLimit(45, 150), 0.0f);   // beyond the chart's reach: 0 kg
        NEAR(c.lookupLimit(10, 0),   0.0f);   // flatter than the chart: 0 kg
        NEAR(c.lookupLimit(14.7f, 0),1.2f);   // edge tolerance (0.5 deg)
        NEAR(c.lookupLimit(14.0f, 0),0.0f);   // ...but not more than that
        NEAR(c.lookupLimit(30, 0.3f),2.5f);   // retracted-encoder noise stays inside the chart
        NEAR(c.lookupLimit(30, -0.4f),2.5f);
        NEAR(c.lookupLimit(30, 6),   1.8f);   // clearly extended: 0 mm rows no longer apply
    }

    // -------------------------------------------------------------- compensation
    SECTION("load compensation");
    {
        resetFlash();
        LoadCompensation g;
        NEAR(g.gainAt(30), 1.0f); NEAR(g.correct(2.0f, 30), 2.0f);   // nothing recorded: no correction

        CHECK(g.record(30, 1.4f, 2.0f) == LCOMP_OK);                  // gain 0.7 at 30 deg
        NEAR(g.gainAt(30), 0.7f); NEAR(g.correct(1.4f, 30), 2.0f);
        NEAR(g.gainAt(10), 0.7f); NEAR(g.gainAt(80), 0.7f);           // single point: constant
        CHECK(g.record(60, 2.0f, 2.0f) == LCOMP_OK);                  // gain 1.0 at 60 deg
        NEAR(g.gainAt(45), 0.85f);                                    // linear between points
        NEAR(g.gainAt(20), 0.7f); NEAR(g.gainAt(75), 1.0f);           // flat outside
        CHECK(g.count() == 2);

        CHECK(g.record(31, 1.6f, 2.0f) == LCOMP_OK);                  // within 2 deg: replaces the 30 point
        CHECK(g.count() == 2); NEAR(g.gainAt(31), 0.8f);

        // Out-of-order recording keeps the table sorted
        CHECK(g.record(10, 1.0f, 2.0f) == LCOMP_OK);
        GainPoint p; CHECK(g.get(0, &p)); NEAR(p.angle, 10);
        CHECK(g.get(1, &p)); NEAR(p.angle, 31); CHECK(g.get(2, &p)); NEAR(p.angle, 60);

        // Rejections
        CHECK(g.record(20, 1.0f, 0.0f)   == LCOMP_ERR_KNOWN_RANGE);
        CHECK(g.record(20, 1.0f, 500.0f) == LCOMP_ERR_KNOWN_RANGE);
        CHECK(g.record(20, 0.01f, 2.0f)  == LCOMP_ERR_NO_LOAD);
        CHECK(g.record(20, 20.0f, 2.0f)  == LCOMP_ERR_GAIN_RANGE);
        CHECK(g.record(20, 0.2f, 2.0f)   == LCOMP_ERR_GAIN_RANGE);
        CHECK(g.count() == 3);

        // Persistence across a power cycle
        LoadCompensation g2; g2.loadFromFlash();
        CHECK(g2.count() == 3); NEAR(g2.gainAt(45), g.gainAt(45));

        // Corrupt gain in flash is ignored (would silently skew every reading)
        GainPoint bad[1] = {{30.0f, 99.0f}};
        Preferences pr; pr.begin("sli_lg", false); pr.putUChar("n", 1); pr.putBytes("p", bad, sizeof(bad)); pr.end();
        LoadCompensation g3; g3.loadFromFlash(); CHECK(g3.count() == 0); NEAR(g3.gainAt(30), 1.0f);

        // Flash failure and busy mutex leave the table unchanged
        Preferences::failWrites() = true;
        CHECK(g.record(70, 1.5f, 2.0f) == LCOMP_ERR_FLASH); CHECK(g.count() == 3);
        Preferences::failWrites() = false;
        g_mutex_available = false;
        CHECK(g.record(70, 1.5f, 2.0f) == LCOMP_ERR_BUSY); CHECK(g.count() == 3);
        g_mutex_available = true;

        // Table full
        LoadCompensation f; resetFlash();
        for (int i = 0; i < LCOMP_MAX_POINTS; i++) CHECK(f.record(10.0f + 10.0f * i, 1.5f, 2.0f) == LCOMP_OK);
        CHECK(f.record(5, 1.5f, 2.0f) == LCOMP_ERR_FULL);
        CHECK(f.record(20, 1.6f, 2.0f) == LCOMP_OK);                  // updating an existing angle still works when full

        CHECK(f.clear() == LCOMP_OK); CHECK(f.count() == 0); NEAR(f.gainAt(30), 1.0f);
        LoadCompensation f2; f2.loadFromFlash(); CHECK(f2.count() == 0);
    }

    // ---------------------------------------------------------- safety evaluate
    SECTION("safety: alarms");
    {
        resetFlash();
        LoadChart chart; LoadCompensation comp; SafetyState st;
        auto eval = [&](float kg, bool ok = true, bool estop = false, float ang = 45, float ext = 0) {
            SafetyInputs in{ang, ext, kg, 0.0f, ok, true, false, true, estop};
            return safetyEvaluate(in, st, chart, comp);
        };

        SafetyOutputs o = eval(0.0f);
        NEAR(o.safeLoadLimit, SAFE_LOAD_DEFAULT_KG); NEAR(o.loadPercent, 0); CHECK(o.alarmLevel == 0);   // no chart: 5 kg default
        o = eval(4.2f);  CHECK(o.alarmLevel == 1); NEAR(o.loadPercent, 84);                                // >= 80%: warn
        o = eval(5.1f);  CHECK(o.alarmLevel == 2);                                                         // >= 100%: critical
        o = eval(4.95f); CHECK(o.alarmLevel == 2);                                                         // 99%: hysteresis holds critical
        o = eval(4.85f); CHECK(o.alarmLevel == 1);                                                         // 97%: steps down to warn
        o = eval(3.95f); CHECK(o.alarmLevel == 1);                                                         // 79%: hysteresis holds warn
        o = eval(3.85f); CHECK(o.alarmLevel == 0);                                                         // 77%: clear

        o = eval(1.0f, /*ok*/false);  NEAR(o.loadPercent, LOAD_PERCENT_SENSOR_FAULT); CHECK(o.alarmLevel == 2);   // stale/dead cell: alarm
        o = eval(12.0f);              NEAR(o.loadPercent, LOAD_PERCENT_SENSOR_FAULT); CHECK(o.alarmLevel == 2);   // out of range: alarm
        o = eval(0.0f);               CHECK(o.alarmLevel == 0);                                                   // recovers
        o = eval(1.0f, true, /*estop*/true);  CHECK(o.alarmLevel == 3);
        o = eval(1.0f, false, true);          CHECK(o.alarmLevel == 3);   // E-stop outranks a sensor fault
        o = eval(1.0f);               CHECK(o.alarmLevel == 0);           // released with RST
        o = eval(9.9f);               NEAR(o.loadPercent, 198); CHECK(o.alarmLevel == 2);                 // 9.9 kg on a 5 kg limit
    }

    SECTION("safety: with chart and compensation");
    {
        resetFlash();
        LoadChart chart; LoadCompensation comp; SafetyState st;
        CHECK(uploadChart(chart, DEMO, 6) == LC_OK);
        auto eval = [&](float kg, float ang, float ext) { SafetyInputs in{ang, ext, kg, 0.0f, true, true, false, true, false}; return safetyEvaluate(in, st, chart, comp); };

        SafetyOutputs o = eval(3.0f, 45, 0);   NEAR(o.safeLoadLimit, 3.8f); NEAR(o.loadPercent, 78.95f); CHECK(o.alarmLevel == 0);
        o = eval(3.9f, 45, 0);                 CHECK(o.alarmLevel == 2);
        o = eval(1.0f, 45, 150);               NEAR(o.safeLoadLimit, 0); NEAR(o.loadPercent, LOAD_PERCENT_CAP); CHECK(o.alarmLevel == 2);  // outside chart
        o = eval(0.01f, 45, 150);              NEAR(o.loadPercent, 0); CHECK(o.alarmLevel == 0);                                            // ...but no load is fine
        o = eval(1.0f, 10, 0);                 NEAR(o.safeLoadLimit, 0); CHECK(o.alarmLevel == 2);

        // Percent is capped (and can never reach the 999 sensor-fault sentinel)
        LoadChart tiny; LoadChartEntry one[1] = {{0, 0, 0.5f}};
        CHECK(uploadChart(tiny, one, 1) == LC_OK);
        { SafetyState st2; SafetyInputs in{45, 0, 9.0f, 0.0f, true, true, false, true, false};
          SafetyOutputs t = safetyEvaluate(in, st2, tiny, comp);
          NEAR(t.loadPercent, LOAD_PERCENT_CAP); CHECK(t.loadPercent < LOAD_PERCENT_SENSOR_FAULT); CHECK(t.alarmLevel == 2); }

        // Angle correction: the cell reads 1.4 kg for a true 2.0 kg at 30 deg
        st.level = 0;
        CHECK(comp.record(30, 1.4f, 2.0f) == LCOMP_OK);
        o = eval(1.4f, 30, 0);                 NEAR(o.actualLoadKg, 2.0f); NEAR(o.safeLoadLimit, 2.5f); NEAR(o.loadPercent, 80); CHECK(o.alarmLevel == 1);
    }



    SECTION("boom angle cross-check (IMU vs encoder)");
    {
        // helper: feed a steady pair for durationMs, 10 ms apart; returns the last result
        uint32_t t = 1000;
        auto feed = [&](BoomAngleCheck& c, float enc, float imu, uint32_t durationMs) {
            BoomCheckResult r = BOOM_CHECK_LEARNING;
            for (uint32_t i = 0; i <= durationMs; i += 10) { r = c.update(t, enc, imu); t += 10; }
            return r;
        };

        // Direction unknown, boom near the reference: nothing to compare yet
        BoomAngleCheck a; a.setReference(100.0f);
        CHECK(feed(a, 100.0f, 0.0f, 200) == BOOM_CHECK_LEARNING);
        CHECK(feed(a, 105.0f, 5.0f, 200) == BOOM_CHECK_LEARNING && a.sign() == 0);      // still under the 10 deg learn threshold
        // Boom raised 12 deg: encoder went UP with it -> direction +1 learned
        CHECK(feed(a, 112.0f, 12.0f, 100) == BOOM_CHECK_OK);
        CHECK(a.sign() == 1 && a.takeLearned() && !a.takeLearned());                    // reported exactly once, for persisting
        CHECK(feed(a, 130.0f, 30.0f, 300) == BOOM_CHECK_OK);                             // keeps agreeing
        CHECK(a.errorDeg() < 0.01f);

        // Encoder that counts the other way: -1 learned
        BoomAngleCheck b; b.setReference(100.0f);
        CHECK(feed(b, 88.0f, 12.0f, 100) == BOOM_CHECK_OK && b.sign() == -1);
        CHECK(feed(b, 70.0f, 30.0f, 300) == BOOM_CHECK_OK);

        // Encoder wraps through 0/360 during the move
        BoomAngleCheck w; w.setReference(355.0f);
        CHECK(feed(w, 7.0f, 12.0f, 100) == BOOM_CHECK_OK && w.sign() == 1);             // 355 -> 7 is +12 deg
        NEAR(BoomAngleCheck::wrap180(350.0f), -10.0f); NEAR(BoomAngleCheck::wrap180(-190.0f), 170.0f); NEAR(BoomAngleCheck::wrap180(180.0f), 180.0f);

        // Tolerance boundary
        BoomAngleCheck c; c.configure(100.0f, 1);
        CHECK(feed(c, 130.0f, 27.1f, 1000) == BOOM_CHECK_OK);                            // 2.9 deg apart: fine
        CHECK(feed(c, 130.0f, 26.5f, 1000) == BOOM_CHECK_MISMATCH);                      // 3.5 deg apart, held: mismatch

        // A brief disagreement (filter lag while moving) is ignored
        BoomAngleCheck d; d.configure(100.0f, 1);
        CHECK(feed(d, 130.0f, 30.0f, 200) == BOOM_CHECK_OK);
        CHECK(feed(d, 140.0f, 30.0f, 300) == BOOM_CHECK_OK);                             // 10 deg apart for only 0.3 s
        CHECK(feed(d, 140.0f, 40.0f, 100) == BOOM_CHECK_OK);                             // caught up again
        CHECK(feed(d, 140.0f, 30.0f, 300) == BOOM_CHECK_OK);                             // the timer restarted: still under 0.5 s
        CHECK(feed(d, 140.0f, 30.0f, 400) == BOOM_CHECK_MISMATCH);                       // and now it has lasted

        // Recovers when the two agree again
        CHECK(feed(d, 140.0f, 40.0f, 100) == BOOM_CHECK_OK);

        // Encoder stuck while the IMU says the boom moved a long way (direction unknown)
        BoomAngleCheck e; e.setReference(100.0f);
        CHECK(feed(e, 100.0f, 30.0f, 200) == BOOM_CHECK_LEARNING);                       // not yet
        CHECK(feed(e, 100.0f, 30.0f, 600) == BOOM_CHECK_MISMATCH);                       // held: encoder did not follow
        // Encoder moves the wrong amount (2:1 gearing, say): mismatch once direction is learned
        BoomAngleCheck g; g.setReference(100.0f);
        CHECK(feed(g, 124.0f, 12.0f, 700) == BOOM_CHECK_MISMATCH);

        // Re-zeroing the IMU sets a new reference and relearns the direction
        c.setReference(50.0f);
        CHECK(c.sign() == 0 && c.refDeg() == 50.0f);
        CHECK(feed(c, 50.0f, 0.0f, 200) == BOOM_CHECK_LEARNING);

        // Restoring persisted state works, and a corrupt direction is not trusted
        BoomAngleCheck h; h.configure(200.0f, -1);
        CHECK(h.sign() == -1 && feed(h, 190.0f, 10.0f, 100) == BOOM_CHECK_OK);
        h.configure(200.0f, 57); CHECK(h.sign() == 1);
        h.configure(200.0f, -57); CHECK(h.sign() == -1);
    }

    SECTION("safety: status flags");
    {
        resetFlash();
        LoadChart chart; LoadCompensation comp; SafetyState st;
        auto eval = [&](float kg, float ang = 45, float ext = 0, float lean = 0, bool loadOK = true,
                        bool boomOK = true, bool extOK = true, bool estop = false, bool mismatch = false) {
            SafetyInputs in{ang, ext, kg, lean, loadOK, boomOK, mismatch, extOK, estop};
            return safetyEvaluate(in, st, chart, comp);
        };
        SafetyOutputs o = eval(1.0f);
        CHECK(o.statusFlags == STATUS_NO_CHART);                         // no chart saved: only the default-limit flag
        CHECK(uploadChart(chart, DEMO, 6) == LC_OK);
        o = eval(1.0f);            CHECK(o.statusFlags == 0);            // healthy, in chart: nothing set
        o = eval(1.0f, 45, 150);   CHECK(o.statusFlags == STATUS_OUT_OF_CHART);
        o = eval(1.0f, 10, 0);     CHECK(o.statusFlags == STATUS_OUT_OF_CHART);

        o = eval(1.0f, 45, 0, 4.9f);  CHECK((o.statusFlags & STATUS_LEAN_WARN) == 0);
        o = eval(1.0f, 45, 0, 5.1f);  CHECK((o.statusFlags & STATUS_LEAN_WARN) != 0); CHECK(o.alarmLevel == 0);   // lean is display only
        o = eval(1.0f, 45, 0, -6.0f); CHECK((o.statusFlags & STATUS_LEAN_WARN) != 0);

        o = eval(1.0f, 45, 0, 0, /*loadOK*/false);  CHECK(o.statusFlags == STATUS_LOAD_FAULT);   CHECK(o.alarmLevel == 2);
        o = eval(12.0f);                            CHECK(o.statusFlags == STATUS_LOAD_FAULT);   CHECK(o.alarmLevel == 2);

        // A dead boom-angle or extension sensor: critical alarm, but the load % stays a real number
        o = eval(0.5f, 45, 0, 0, true, /*boomOK*/false);
        CHECK(o.statusFlags == STATUS_BOOM_FAULT); CHECK(o.alarmLevel == 2); CHECK(o.loadPercent < LOAD_PERCENT_SENSOR_FAULT);
        o = eval(0.5f, 45, 0, 0, true, true, /*extOK*/false);
        CHECK(o.statusFlags == STATUS_EXT_FAULT);  CHECK(o.alarmLevel == 2);
        o = eval(0.5f);            CHECK(o.statusFlags == 0); CHECK(o.alarmLevel == 0);               // recovers when the sensor returns
        o = eval(0.5f, 45, 0, 0, true, true, true, false, /*mismatch*/true);
        CHECK(o.statusFlags == STATUS_BOOM_MISMATCH); CHECK(o.alarmLevel == 2); CHECK(o.loadPercent < LOAD_PERCENT_SENSOR_FAULT);
        o = eval(0.5f);            CHECK(o.statusFlags == 0); CHECK(o.alarmLevel == 0);

        o = eval(1.0f, 45, 0, 0, true, true, true, /*estop*/true);
        CHECK((o.statusFlags & STATUS_ESTOP) != 0); CHECK(o.alarmLevel == 3);
        o = eval(1.0f, 45, 0, 0, true, false, true, true);
        CHECK((o.statusFlags & (STATUS_ESTOP | STATUS_BOOM_FAULT)) == (STATUS_ESTOP | STATUS_BOOM_FAULT)); CHECK(o.alarmLevel == 3);
    }

    SECTION("telemetry packet layout");
    {
        TelemetryFormatter fmt; char buf[256];
        SensorData d; memset(&d, 0, sizeof(d));
        d.boomAngle = 45.5f; d.extensionMM = 120.0f; d.loadCellRaw = 1.4f; d.actualLoadKg = 2.0f;
        d.swingAngle = 10.0f; d.ropeLengthMM = 300.0f; d.fsr[0] = 100; d.fsr[1] = 200; d.fsr[2] = 300; d.fsr[3] = 400;
        d.boomLean = -2.5f; d.statusFlags = STATUS_LOAD_FAULT | STATUS_LEAN_WARN;
        d.safeLoadLimit = 5.0f; d.loadPercent = 40.0f; d.alarmLevel = 1;
        fmt.format(d, buf, sizeof(buf));
        std::string line(buf);
        CHECK(line.compare(0, 3, "$T,") == 0 && line.back() == '\n');
        std::vector<std::string> f; size_t start = 0;
        for (size_t i = 0; i <= line.size(); i++) if (i == line.size() || line[i] == ',' || line[i] == '\n') { f.push_back(line.substr(start, i - start)); start = i + 1; }
        if (!f.empty() && f.back().empty()) f.pop_back();
        CHECK(f.size() == 16);                           // "$T" + 15 fields
        if (f.size() == 16) {
            CHECK(f[3] == "1.40");                       // field 3: measured load in kg (not raw counts)
            CHECK(f[4] == "2.00");                       // field 4: corrected load
            CHECK(f[11] == "-2.50");                     // field 11: boom lean
            CHECK(f[12] == "33");                        // field 12: status flags (bit0 + bit5)
            CHECK(f[15] == "1");                         // alarm level last
        }
    }

    // ------------------------------------------------------------- debug report
    SECTION("debug report gain lines");
    {
        TelemetryFormatter fmt; char buf[768]; uint16_t fsr[4] = {4095, 4095, 4095, 4095};
        GainPoint pts[LCOMP_MAX_POINTS];
        for (int i = 0; i < LCOMP_MAX_POINTS; i++) pts[i] = { 10.0f + 10.0f * i, 0.7f + 0.01f * i };
        fmt.formatDebugReport(true, true, true, true, true, fsr, 123456789L, 19.048f, true, pts, LCOMP_MAX_POINTS, 3, 4.5f, buf, sizeof(buf));
        std::string s(buf);
        CHECK(s.find("$D,GAIN,10.0,0.7000\n") != std::string::npos);
        CHECK(s.find("$D,GAIN,80.0,0.7700\n") != std::string::npos);
        CHECK(s.find("$D,TELE_SCALE,19.0480\n") != std::string::npos);
        CHECK(s.find("$D,BOOM_XCHECK,4.5,MISMATCH\n") != std::string::npos);
        CHECK(s.size() >= 3 && s.substr(s.size() - 7) == "$D,END\n");                  // not truncated with 8 points
        printf("  worst-case report size: %zu of %zu bytes\n", s.size(), sizeof(buf));
        CHECK(s.size() < sizeof(buf) - 64);
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
