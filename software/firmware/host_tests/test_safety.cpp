// ============================================================
//  PC unit tests for esp32_mainv2 load_chart.cpp + safety_logic.cpp (pure logic)
//
//  Build + run from software/firmware:
//    g++ -std=c++17 -Wall -Wextra -I esp32_mainv2 -I host_tests host_tests/test_safety.cpp esp32_mainv2/load_chart.cpp esp32_mainv2/safety_logic.cpp -o host_tests/test_safety && host_tests/test_safety
// ============================================================
#include "load_chart.h"
#include "safety_logic.h"
#include <cmath>
#include <cstdio>
#include <cstring>

#include "check.h"
#include <initializer_list>

// Test chart: angles 0/10/20/30 deg, extensions 20/40/60 mm, rear zone at half capacity
static const char* CHART[] = {
    "BEGIN 4 3 TEST CHART",
    "A 0 10 20 30",
    "E 20 40 60",
    "R 0 0.6 0.5 0.4",
    "R 1 0.7 0.6 0.5",
    "R 2 0.8 0.7 0.6",
    "R 3 0.9 0.8 0.7",
    "Z 135 225 0.5",
};

static LoadChart buildChart() {
    LoadChartStage st;
    const char* why;
    for (const char* line : CHART) CHECK(lc_parse(&st, line, &why) == LC_PARSE_OK);
    CHECK(lc_parse(&st, "COMMIT", &why) == LC_PARSE_COMMIT);
    CHECK(lc_stageComplete(st, &why));
    CHECK(lc_validate(st.chart, &why));
    return st.chart;
}

static SafetyInputs pose(float boom, float ext, float swing, float load) {
    SafetyInputs in = {};   // rates 0, PREDICTIVE
    in.boomDeg = boom; in.extMm = ext; in.swingDeg = swing; in.loadKg = load; in.sensorsOK = true;
    return in;
}

static bool blocked(const SafetyResult& r, uint8_t axis, uint8_t dir) {
    return (r.blocked & sli_bit(axis, dir)) != 0;
}

static void test_parse() {
    const LoadChart c = buildChart();
    CHECK(c.nA == 4 && c.nE == 3 && c.nZ == 1);
    CHECK(std::strcmp(c.name, "TEST CHART") == 0);
    CHECK_NEAR(c.warnPct, 90.0, 1e-6);
    CHECK_NEAR(c.cutPct, 100.0, 1e-6);

    LoadChartStage st;
    std::memset(&st, 0, sizeof(st));
    const char* why;
    CHECK(lc_parse(&st, "R 0 1 2 3", &why) == LC_PARSE_ERR);          // before BEGIN
    CHECK(lc_parse(&st, "BEGIN 1 3 X", &why) == LC_PARSE_ERR);        // too few angles
    CHECK(lc_parse(&st, "BEGIN 4 3 X", &why) == LC_PARSE_OK);
    CHECK(lc_parse(&st, "A 0 10 20", &why) == LC_PARSE_ERR);          // 3 values for 4 angles
    CHECK(lc_parse(&st, "A 0 10 20 30 40", &why) == LC_PARSE_ERR);    // 5 values
    CHECK(lc_parse(&st, "R 4 1 1 1", &why) == LC_PARSE_ERR);          // row out of range
    CHECK(lc_parse(&st, "R 0 1 1 x", &why) == LC_PARSE_ERR);          // junk
    CHECK(lc_parse(&st, "A 0 10 20 30", &why) == LC_PARSE_OK);
    CHECK(lc_parse(&st, "E 20 40 60", &why) == LC_PARSE_OK);
    CHECK(lc_parse(&st, "R 0 1 1 1", &why) == LC_PARSE_OK);
    CHECK(!lc_stageComplete(st, &why) && std::strcmp(why, "MISSING_ROWS") == 0);
    CHECK(lc_parse(&st, "P 95 100", &why) == LC_PARSE_OK && st.chart.warnPct == 95.0f);
    CHECK(lc_parse(&st, "GET", &why) == LC_PARSE_GET);
    CHECK(lc_parse(&st, "FOO", &why) == LC_PARSE_ERR);
}

static void test_validate() {
    const char* why;
    LoadChart c = buildChart();
    LoadChart bad = c; bad.angDeg[2] = bad.angDeg[1];           CHECK(!lc_validate(bad, &why));
    bad = c; bad.capKg[1][1] = 0.0f;                            CHECK(!lc_validate(bad, &why));
    bad = c; bad.capKg[1][1] = 50.0f;                           CHECK(!lc_validate(bad, &why));
    bad = c; bad.warnPct = 100.0f;                              CHECK(!lc_validate(bad, &why));
    bad = c; bad.zones[0].factor = 0.0f;                        CHECK(!lc_validate(bad, &why));
    bad = c; bad.version = 99;                                  CHECK(!lc_validate(bad, &why));
}

static void test_capacity() {
    const LoadChart c = buildChart();
    int8_t z;
    CHECK_NEAR(lc_capacity(c, 10, 40, 0, &z), 0.6, 1e-5);       // exact cell
    CHECK(z == -1);
    CHECK_NEAR(lc_capacity(c, 15, 30, 0, &z), 0.70, 1e-5);      // bilinear centre of 0.7/0.6/0.8/0.7
    CHECK_NEAR(lc_capacity(c, 10, 30, 0, &z), 0.65, 1e-5);      // halfway along one row
    CHECK_NEAR(lc_capacity(c, -0.5f, 20, 0, &z), 0.6, 1e-5);    // inside tolerance: clamped
    CHECK_NEAR(lc_capacity(c, -2, 20, 0, &z), 0.0, 1e-9);       // outside chart
    CHECK_NEAR(lc_capacity(c, 10, 63, 0, &z), 0.0, 1e-9);       // past max extension + tol
    CHECK_NEAR(lc_capacity(c, 10, 40, 180, &z), 0.3, 1e-5);     // rear zone x0.5
    CHECK(z == 0);
    CHECK_NEAR(lc_capacity(c, 10, 40, NAN, &z), 0.6, 1e-5);     // NaN swing: no zone, factor 1
    CHECK_NEAR(lc_capacity(c, NAN, 40, 0, &z), 0.0, 1e-9);      // NaN angle: outside
}

static void test_zones() {
    LoadChart c = buildChart();
    c.zones[0].fromDeg = 300; c.zones[0].toDeg = 60;            // wraps through 0
    int8_t z;
    CHECK_NEAR(lc_zoneFactor(c, 350, &z), 0.5, 1e-6);
    CHECK_NEAR(lc_zoneFactor(c, 10, &z), 0.5, 1e-6);
    CHECK_NEAR(lc_zoneFactor(c, -10, &z), 0.5, 1e-6);           // -10 = 350
    CHECK_NEAR(lc_zoneFactor(c, 180, &z), 1.0, 1e-6);
    CHECK(z == -1);
}

static void test_fault() {
    SafetyResult r = sli_evaluate(nullptr, pose(10, 40, 0, 0.1f), SLI_OK);
    CHECK(r.state == SLI_FAULT);
    CHECK(!blocked(r, 3, 0) && !blocked(r, 4, 1));                        // retract, pay out
    CHECK(blocked(r, 2, 1));                                              // no chart: boom up unknown -> cut
    CHECK(!blocked(r, 2, 0));                                             // boom down: never cut
    CHECK(blocked(r, 3, 1) && blocked(r, 4, 0));                          // extend, hoist
    CHECK(blocked(r, 1, 0) && blocked(r, 1, 1));                          // swing

    const LoadChart c = buildChart();
    SafetyInputs in = pose(10, 40, 0, 0.1f);
    in.sensorsOK = false;
    CHECK(sli_evaluate(&c, in, SLI_OK).state == SLI_FAULT);
    CHECK(sli_evaluate(&c, pose(10, 40, 0, 50.0f), SLI_OK).state == SLI_FAULT);  // impossible load
    CHECK(sli_evaluate(&c, pose(10, 40, 0, NAN), SLI_OK).state == SLI_FAULT);
}

static void test_normal() {
    const LoadChart c = buildChart();
    const SafetyResult r = sli_evaluate(&c, pose(10, 40, 0, 0.3f), SLI_OK);   // 50 %
    CHECK(r.state == SLI_OK);
    CHECK_NEAR(r.pct, 50.0, 1e-3);
    CHECK_NEAR(r.capKg, 0.6, 1e-5);
    CHECK(r.blocked == 0);
}

static void test_cut_selective() {
    const LoadChart c = buildChart();
    const SafetyResult r = sli_evaluate(&c, pose(10, 40, 0, 0.6f), SLI_WARN);  // exactly 100 %
    CHECK(r.state == SLI_CUT);
    CHECK(!blocked(r, 2, 0));     // boom down: never cut (SLI_BOOM_DOWN_ALWAYS)
    CHECK(!blocked(r, 2, 1));     // boom up: recovery allowed
    CHECK(blocked(r, 3, 1));      // extend
    CHECK(!blocked(r, 3, 0));     // retract allowed
    CHECK(blocked(r, 4, 0));      // hoist
    CHECK(!blocked(r, 4, 1));     // pay out allowed
    CHECK(blocked(r, 1, 0) && blocked(r, 1, 1));   // swing doesn't reduce the % here: held
}

static void test_predictive() {
    const LoadChart c = buildChart();
    // 99 %: not yet CUT, but one degree down crosses 100 %
    const SafetyResult r = sli_evaluate(&c, pose(10, 40, 0, 0.594f), SLI_OK);
    CHECK(r.state == SLI_WARN);
    CHECK(!blocked(r, 2, 0));     // boom down: never cut
    CHECK(blocked(r, 3, 1));      // extending 2 mm crosses 100 %: cut ahead of time
    CHECK(!blocked(r, 2, 1));
    CHECK(!blocked(r, 4, 0));     // hoist only cut in CUT state
}

static void test_hysteresis() {
    const LoadChart c = buildChart();
    CHECK(sli_evaluate(&c, pose(10, 40, 0, 0.594f), SLI_CUT).state == SLI_CUT);   // 99 %
    CHECK(sli_evaluate(&c, pose(10, 40, 0, 0.582f), SLI_CUT).state == SLI_WARN);  // 97 %
    CHECK(sli_evaluate(&c, pose(10, 40, 0, 0.534f), SLI_WARN).state == SLI_WARN); // 89 %
    CHECK(sli_evaluate(&c, pose(10, 40, 0, 0.522f), SLI_WARN).state == SLI_OK);   // 87 %
}

static void test_soft_limits() {
    const LoadChart c = buildChart();
    // Empty hook at the bottom edge of the chart: boom down is not cut by the LOAD
    // logic, only by the end stop (SLI_BOOM_MIN_DEG); just above it, it is free
    SafetyResult r = sli_evaluate(&c, pose(-0.4f, 40, 0, 0.0f), SLI_OK);
    CHECK(r.state == SLI_OK);
    CHECK(!blocked(r, 2, 0));
    CHECK(!blocked(r, 2, 1));
    // Fully extended (last column + inside the 2 mm tolerance band): extending
    // further is cut, retracting is fine. The limit sits at max + LC_TOL_MM.
    r = sli_evaluate(&c, pose(10, 60, 0, 0.0f), SLI_OK);
    CHECK(!blocked(r, 3, 1));     // 62 mm is still inside the band
    r = sli_evaluate(&c, pose(10, 61, 0, 0.0f), SLI_OK);
    CHECK(blocked(r, 3, 1));
    CHECK(!blocked(r, 3, 0));
}

static void test_outside_not_stuck() {
    const LoadChart c = buildChart();
    // Boom parked 10 deg below the chart: must still be able to raise it back in
    const SafetyResult r = sli_evaluate(&c, pose(-10, 40, 0, 0.0f), SLI_OK);
    CHECK(r.state == SLI_CUT);
    CHECK_NEAR(r.pct, SLI_PCT_MAX, 1e-3);
    CHECK(!blocked(r, 2, 1));     // boom up: toward the chart
    CHECK(blocked(r, 2, 0));      // boom down: below the end stop (SLI_BOOM_MIN_DEG)
    CHECK(blocked(r, 3, 1));      // extend: still cut out here
}

static void test_swing_zone() {
    const LoadChart c = buildChart();
    // 2 deg before the rear zone (half capacity) at 67 %: swinging into it would be 133 %
    const SafetyResult r = sli_evaluate(&c, pose(10, 40, 133, 0.4f), SLI_OK);
    CHECK(r.state == SLI_OK);
    CHECK(blocked(r, 1, 1));      // toward the rear zone (D1 = +angle)
    CHECK(!blocked(r, 1, 0));     // away from it
}

static void test_reason() {
    const LoadChart c = buildChart();
    CHECK(sli_evaluate(nullptr, pose(10, 40, 0, 0.1f), SLI_OK).reason == SLI_WHY_NO_CHART);
    SafetyInputs in = pose(10, 40, 0, 0.1f);
    in.sensorsOK = false;
    CHECK(sli_evaluate(&c, in, SLI_OK).reason == SLI_WHY_SENSOR);
    CHECK(sli_evaluate(&c, pose(10, 40, 0, 25.0f), SLI_OK).reason == SLI_WHY_LOAD);
    CHECK(sli_evaluate(&c, pose(NAN, 40, 0, 0.1f), SLI_OK).reason == SLI_WHY_BAD_VALUE);
    CHECK(sli_evaluate(&c, pose(-5, 40, 0, 0.0f), SLI_OK).reason == SLI_WHY_OUTSIDE);
    const SafetyResult ok = sli_evaluate(&c, pose(10, 40, 0, 0.0f), SLI_OK);
    CHECK(ok.reason == SLI_WHY_NONE && ok.blocked == 0);           // no load, mid-chart: all free
    CHECK(std::strcmp(sli_reasonName(SLI_WHY_OUTSIDE), "OUTSIDE") == 0);
}

/** MS's case: capacity falls as the boom rises (motor stall limit). With 1 kg, boom up
 *  must run until just before the angle where 1 kg is 100 %, then stop; down stays free. */
static void test_lift_until_limit() {
    LoadChart c = buildChart();
    c.nZ = 0;
    // rows 0, 10, 20, 30 deg: 1.2, 0.9, 0.8, 0.7 kg at every extension
    const float rows[4] = {1.2f, 0.9f, 0.8f, 0.7f};
    for (uint8_t i = 0; i < c.nA; i++) for (uint8_t j = 0; j < c.nE; j++) c.capKg[i][j] = rows[i];
    // 1 kg = 100 % at 6.67 deg; the 1 deg look-ahead cuts boom up from ~5.67 deg
    SafetyResult r = sli_evaluate(&c, pose(5.0f, 40, 0, 1.0f), SLI_OK);
    CHECK(r.state != SLI_CUT && !blocked(r, 2, 1));
    r = sli_evaluate(&c, pose(6.0f, 40, 0, 1.0f), SLI_OK);
    CHECK(blocked(r, 2, 1));      // boom up cut before 6.67 deg
    CHECK(!blocked(r, 2, 0));     // boom down allowed (capacity rises)
    CHECK(!blocked(r, 3, 0) && !blocked(r, 3, 1));   // extension doesn't change capacity here
}

/** Same falling chart: where does each cut mode stop boom-up with 1 kg (100 % at 6.67 deg)? */
static LoadChart fallingChart() {
    LoadChart c = buildChart();
    c.nZ = 0;
    const float rows[4] = {1.2f, 0.9f, 0.8f, 0.7f};
    for (uint8_t i = 0; i < c.nA; i++) for (uint8_t j = 0; j < c.nE; j++) c.capKg[i][j] = rows[i];
    return c;
}

/** First boom angle (0.05 deg steps from 0 deg) at which boom up is cut. */
static float boomUpCutAt(const LoadChart& c, SliCutMode mode, float boomRate) {
    SliState prev = SLI_OK;
    for (float b = 0.0f; b < 12.0f; b += 0.05f) {
        SafetyInputs in = pose(b, 40, 0, 1.0f);
        in.cutMode = mode;
        in.boomRate = boomRate;
        const SafetyResult r = sli_evaluate(&c, in, prev);
        prev = r.state;
        if (blocked(r, 2, 1)) return b;
    }
    return 99.0f;
}

static void test_cut_modes() {
    const LoadChart c = fallingChart();
    const float limit = 6.667f;
    const float pred  = boomUpCutAt(c, SLI_CUT_PREDICTIVE, 2.0f);
    const float react = boomUpCutAt(c, SLI_CUT_REACTIVE, 2.0f);
    // SPEED: look-ahead = margin + v x T
    const float creep = boomUpCutAt(c, SLI_CUT_SPEED, 0.3f);    // 0.25 + 0.15 = 0.4 deg ahead
    const float real  = boomUpCutAt(c, SLI_CUT_SPEED, 1.8f);    // 0.25 + 0.9  = 1.15 deg (crane's boom speed)
    const float fast  = boomUpCutAt(c, SLI_CUT_SPEED, 4.0f);    // 0.25 + 2.0  = 2.25 deg
    CHECK_NEAR(pred, limit - SLI_LOOK_BOOM_DEG, 0.06);          // one fixed step early
    CHECK(react >= limit - 0.01f && react < limit + 0.06f);      // only once 100 % is reached
    CHECK_NEAR(creep, limit - (SLI_SPEED_MARGIN_BOOM_DEG + 0.3f * SLI_LOOK_TIME_S), 0.06);
    CHECK_NEAR(real,  limit - (SLI_SPEED_MARGIN_BOOM_DEG + 1.8f * SLI_LOOK_TIME_S), 0.06);
    CHECK_NEAR(fast,  limit - (SLI_SPEED_MARGIN_BOOM_DEG + 4.0f * SLI_LOOK_TIME_S), 0.06);
    CHECK(creep > pred && fast < pred);                          // slow gets closer than P, fast stops earlier

    // Moving AWAY from the limit (or standing still) leaves only the margin
    const float away = boomUpCutAt(c, SLI_CUT_SPEED, -12.0f);
    CHECK_NEAR(away, limit - SLI_SPEED_MARGIN_BOOM_DEG, 0.06);
    CHECK_NEAR(boomUpCutAt(c, SLI_CUT_SPEED, 0.0f), away, 0.01f);

    // In every mode the recovering direction stays free at and beyond the limit
    for (SliCutMode m : { SLI_CUT_PREDICTIVE, SLI_CUT_REACTIVE, SLI_CUT_SPEED }) {
        SafetyInputs in = pose(8.0f, 40, 0, 1.0f);              // past the limit: 1 kg > 0.87 kg rated
        in.cutMode = m;
        const SafetyResult r = sli_evaluate(&c, in, SLI_CUT);
        CHECK(r.state == SLI_CUT);
        CHECK(blocked(r, 2, 1));
        CHECK(!blocked(r, 2, 0));
        CHECK(blocked(r, 4, SLI_WINCH_HOIST_DIR));
    }
    // Reactive below the limit: nothing cut even one step before it
    SafetyInputs in = pose(6.3f, 40, 0, 1.0f);
    in.cutMode = SLI_CUT_REACTIVE;
    CHECK(!blocked(sli_evaluate(&c, in, SLI_WARN), 2, 1));
    CHECK(sli_cutModeLetter(SLI_CUT_REACTIVE) == 'R' && sli_cutModeLetter(SLI_CUT_SPEED) == 'S' &&
          sli_cutModeLetter(SLI_CUT_PREDICTIVE) == 'P');
}

/** FAULT must never free a direction the chart can't prove safe (MS, 18 Nov:
 *  CUT -> IMU dropout -> FAULT unlocked boom up past the limit). */
static void test_fault_from_chart() {
    // This crane: capacity FALLS with boom angle -> boom up is the critical direction
    const LoadChart fall = fallingChart();
    SafetyInputs in = pose(7.0f, 40, 0, 1.0f);
    in.sensorsOK = false;
    const SafetyResult f = sli_evaluate(&fall, in, SLI_CUT);
    CHECK(f.state == SLI_FAULT);
    CHECK(blocked(f, 2, 1));                                  // boom up cut
    CHECK(!blocked(f, 2, 0));                                 // boom down free (SLI_BOOM_DOWN_ALWAYS)
    CHECK(!blocked(f, 3, 0) && blocked(f, 3, 1));             // retract free, extend cut
    CHECK(!blocked(f, 4, 1 - SLI_WINCH_HOIST_DIR) && blocked(f, 4, SLI_WINCH_HOIST_DIR));
    CHECK(blocked(f, 1, 0) && blocked(f, 1, 1));              // swing cut
    // Real-crane chart: capacity RISES with angle -> boom up provably never worse
    const LoadChart rise = buildChart();
    const SafetyResult g = sli_evaluate(&rise, in, SLI_OK);
    CHECK(g.state == SLI_FAULT && !blocked(g, 2, 1) && !blocked(g, 3, 0));
    // A chart where capacity rises with extension somewhere: retract not proven safe
    LoadChart odd = rise;
    odd.capKg[2][0] = odd.capKg[2][1] - 0.05f;               // 20 mm below 40 mm: retract can lose capacity
    CHECK(blocked(sli_evaluate(&odd, in, SLI_OK), 3, 0));
}

/** Grace: a short pose dropout from OK keeps the last decision; everything else strict. */
static void test_fault_grace() {
    const uint8_t strict = (uint8_t)~(sli_bit(3, 0) | sli_bit(4, 1));   // FAULT: retract + pay out only
    const uint8_t lastOk = 0;                                         // OK, nothing cut
    // OK before, IMU only, 1 s: crane keeps running as before
    CHECK(sli_faultBlocked(strict, lastOk, SLI_OK, 1000, true) == lastOk);
    CHECK(sli_faultBlocked(strict, lastOk, SLI_OK, SLI_FAULT_GRACE_MS, true) == lastOk);
    // Longer than the grace: strict
    CHECK(sli_faultBlocked(strict, lastOk, SLI_OK, SLI_FAULT_GRACE_MS + 1, true) == strict);
    // WARN or CUT before the dropout: strict, and the old cuts stay cut
    const uint8_t cutUp = sli_bit(2, 1);
    CHECK(sli_faultBlocked(strict, 0, SLI_WARN, 100, true) == strict);
    CHECK(sli_faultBlocked(0, cutUp, SLI_CUT, 100, true) == cutUp);
    // Load cell (not pose-only): strict even from OK
    CHECK(sli_faultBlocked(strict, lastOk, SLI_OK, 100, false) == strict);
    // No valid evaluation yet (boot): strict
    CHECK(sli_faultBlocked(strict, lastOk, SLI_FAULT, 100, true) == strict);
    // Grace never frees what was cut while OK (e.g. a chart edge)
    CHECK(sli_faultBlocked(strict, cutUp, SLI_OK, 100, true) == cutUp);
}

static void test_boom_travel_limit() {
    const LoadChart c = buildChart();
    // Boom on its stop (IMU below -0.5 deg): boom down cut, in every state, even with no chart
    SafetyInputs low = pose(-0.6f, 40, 0, 0.1f);
    CHECK(sli_travelLimits(low, nullptr) == sli_bit(2, 0));
    CHECK(sli_travelLimits(low, &c) == sli_bit(2, 0));               // chart default -0.5, ext stop off
    CHECK(blocked(sli_evaluate(&c, low, SLI_OK), 2, 0));
    CHECK(blocked(sli_evaluate(nullptr, low, SLI_OK), 2, 0));         // FAULT keeps the end stop too
    CHECK(!blocked(sli_evaluate(&c, low, SLI_OK), 2, 1));             // boom up still free
    // Just above the stop: free
    CHECK(sli_travelLimits(pose(-0.4f, 40, 0, 0.1f), &c) == 0);
    CHECK(sli_travelLimits(pose(NAN, 40, 0, 0.1f), &c) == 0);
}

/** Per-chart stops (LC L): boom min and telescope min set by MS (19 Nov). */
static void test_chart_travel_stops() {
    LoadChartStage st;
    const char* why;
    for (const char* line : CHART) CHECK(lc_parse(&st, line, &why) == LC_PARSE_OK);
    CHECK(lc_parse(&st, "L -0.1 5", &why) == LC_PARSE_OK);
    CHECK(lc_parse(&st, "COMMIT", &why) == LC_PARSE_COMMIT);
    CHECK(lc_validate(st.chart, &why));
    const LoadChart& c = st.chart;
    CHECK_NEAR(c.boomMinDeg, -0.1, 1e-6);
    CHECK_NEAR(c.extMinMm, 5.0, 1e-6);
    CHECK(sli_travelLimits(pose(-0.2f, 40, 0, 0.1f), &c) == sli_bit(2, 0));   // below -0.1: boom down cut
    CHECK(sli_travelLimits(pose(0.0f, 40, 0, 0.1f), &c) == 0);
    CHECK(sli_travelLimits(pose(10, 5.0f, 0, 0.1f), &c) == sli_bit(3, 0));    // at 5 mm: retract cut
    CHECK(sli_travelLimits(pose(10, 6.0f, 0, 0.1f), &c) == 0);
    CHECK(!blocked(sli_evaluate(&c, pose(10, 4.0f, 0, 0.1f), SLI_OK), 3, 1)); // extend stays free
    // Out-of-range stops are rejected
    LoadChart bad = c;
    bad.boomMinDeg = -45.0f;
    CHECK(!lc_validate(bad, &why));
    CHECK(lc_parse(&st, "L 1", &why) == LC_PARSE_ERR);
}

int main() {
    test_parse();
    test_validate();
    test_capacity();
    test_zones();
    test_fault();
    test_normal();
    test_cut_selective();
    test_predictive();
    test_hysteresis();
    test_soft_limits();
    test_outside_not_stuck();
    test_swing_zone();
    test_reason();
    test_lift_until_limit();
    test_cut_modes();
    test_boom_travel_limit();
    test_fault_from_chart();
    test_fault_grace();
    test_chart_travel_stops();
    return check_summary("safety");
}
