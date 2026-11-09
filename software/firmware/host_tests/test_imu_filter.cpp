// ============================================================
//  PC unit tests for esp32_mainv2/imu_filter.h (boom-tip IMU filter)
//
//  Build + run from software/firmware:
//    g++ -std=c++17 -Wall -Wextra -I esp32_mainv2 -I host_tests host_tests/test_imu_filter.cpp -o host_tests/test_imu_filter && host_tests/test_imu_filter
// ============================================================
#include "imu_filter.h"
#include "check.h"
#include <initializer_list>

static const float DT = 0.01f;   // 100 Hz, like HwBusTask

/** Gravity seen by a sensor at this roll (pitch 0), plus an optional extra acceleration. */
static Vec3 accAt(float rollDeg, Vec3 extra = {0, 0, 0}) {
    const Vec3 g = gravityAt(rollDeg, 0.0f);
    return { g.x + extra.x, g.y + extra.y, g.z + extra.z };
}

static void test_median3() {
    Median3 m;
    float out[6];
    const float in[6] = { 1, 1, 50, 1, 1, 1 };       // one corrupted sample
    float worst = 0;
    for (int i = 0; i < 6; i++) { out[i] = m.push(in[i]); if (out[i] > worst) worst = out[i]; }
    CHECK_NEAR(worst, 1.0f, 1e-6);                   // the spike never comes out
    Median3 s;
    const float step[5] = { 0, 0, 5, 5, 5 };         // a real step passes, one sample late
    for (int i = 0; i < 5; i++) out[i] = s.push(step[i]);
    CHECK_NEAR(out[2], 0.0f, 1e-6);
    CHECK_NEAR(out[3], 5.0f, 1e-6);
}

static void test_gravity_round_trip() {
    for (float r : { -170.0f, -45.0f, 0.0f, 30.0f, 179.0f }) {
        for (float p : { -40.0f, 0.0f, 25.0f }) {
            const Vec3 g = gravityAt(r, p);
            CHECK_NEAR(accelRollDeg(g), r, 1e-3);
            CHECK_NEAR(accelPitchDeg(g), p, 1e-3);
            CHECK_NEAR(vecLen(g), IMU_G, 1e-3);
        }
    }
}

static void test_converges_with_tau() {
    ImuFilter f;                                  // tau 1 s
    f.seed(accAt(10.0f));
    float r = 0;
    for (int i = 0; i < 100; i++) r = f.update(accAt(20.0f), {0, 0, 0}, DT).rollDeg;   // 1 s
    CHECK(r > 15.5f && r < 17.0f);                // ~63 % of the 10 deg step after one tau
    for (int i = 0; i < 500; i++) r = f.update(accAt(20.0f), {0, 0, 0}, DT).rollDeg;   // +5 s
    CHECK_NEAR(r, 20.0f, 0.05);
}

static void test_tracks_rotation_with_gyro() {
    ImuFilter f;
    f.seed(accAt(0.0f));
    float r = 0, truth = 0;
    for (int i = 0; i < 300; i++) {               // 3 s at 10 deg/s
        truth += 10.0f * DT;
        r = f.update(accAt(truth), {10.0f, 0, 0}, DT).rollDeg;
    }
    CHECK_NEAR(r, truth, 0.2);                    // gyro carries it: no 1 s lag
}

static void test_gyro_spike_rejected() {
    ImuFilter f;
    f.seed(accAt(15.0f));
    for (int i = 0; i < 50; i++) f.update(accAt(15.0f), {0, 0, 0}, DT);
    float r = 0, worst = 0;
    for (int i = 0; i < 50; i++) {
        const Vec3 gyro = (i == 10) ? Vec3{90.0f, 0, 0} : Vec3{0, 0, 0};   // one corrupted transfer
        r = f.update(accAt(15.0f), gyro, DT).rollDeg;
        if (fabsf(r - 15.0f) > worst) worst = fabsf(r - 15.0f);
    }
    CHECK(worst < 0.01f);                          // without the median: 0.9 deg jump
}

static void test_jerk_does_not_tilt_angle() {
    // 0.3 s sideways shake of 6 m/s^2 (boom stopping hard), no real rotation
    auto run = [](float gate) {
        ImuFilter f;
        f.gateG = gate;
        f.seed(accAt(20.0f));
        float worst = 0;
        for (int i = 0; i < 100; i++) {
            const Vec3 extra = (i >= 20 && i < 50) ? Vec3{0, 6.0f, 0} : Vec3{0, 0, 0};
            const float r = f.update(accAt(20.0f, extra), {0, 0, 0}, DT).rollDeg;
            if (fabsf(r - 20.0f) > worst) worst = fabsf(r - 20.0f);
        }
        return worst;
    };
    const float gated   = run(0.15f);
    const float ungated = run(100.0f);             // plain complementary filter
    CHECK(gated < 0.05f);
    CHECK(ungated > 1.0f);
}

static void test_dynamic_accel_and_jerk() {
    ImuFilter f;
    f.seed(accAt(30.0f));
    ImuFilterOutput o{};
    for (int i = 0; i < 100; i++) o = f.update(accAt(30.0f), {0, 0, 0}, DT);
    CHECK(o.accelDyn < 0.01f);                     // at rest: only gravity
    CHECK(o.jerk < 0.01f);

    float peakDyn = 0, peakJerk = 0;
    for (int i = 0; i < 20; i++) {                 // 2 m/s^2 step along x, held
        o = f.update(accAt(30.0f, {2.0f, 0, 0}), {0, 0, 0}, DT);
        if (o.accelDyn > peakDyn)  peakDyn = o.accelDyn;
        if (o.jerk > peakJerk)     peakJerk = o.jerk;
    }
    CHECK_NEAR(peakDyn, 2.0f, 0.05);
    CHECK_NEAR(peakJerk, 200.0f, 1.0);             // 2 m/s^2 in one 10 ms sample
}

static void test_roll_wraps_at_180() {
    // Sensor upside down: roll hovers around +-180 with noise across the wrap
    ImuFilter f;
    f.seed(accAt(179.5f));
    float worst = 0;
    for (int i = 0; i < 400; i++) {
        const float truth = (i % 2) ? 179.0f : -179.0f;
        const float r = f.update(accAt(truth), {0, 0, 0}, DT).rollDeg;
        const float err = fabsf(wrapDeg180(r - 180.0f));
        if (err > worst) worst = err;
    }
    CHECK(worst < 1.5f);                           // never swings through 0
}

int main() {
    test_median3();
    test_gravity_round_trip();
    test_converges_with_tau();
    test_tracks_rotation_with_gyro();
    test_gyro_spike_rejected();
    test_jerk_does_not_tilt_angle();
    test_dynamic_accel_and_jerk();
    test_roll_wraps_at_180();
    return check_summary("imu_filter");
}
