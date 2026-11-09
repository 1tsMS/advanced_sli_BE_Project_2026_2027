// ============================================================
//  PC unit tests for the load-cell boom-angle correction
//    esp32_mainv2/load_angle.h
//
//  Build + run from software/firmware:
//    g++ -std=c++17 -Wall -Wextra -I esp32_mainv2 -I host_tests host_tests/test_load_angle.cpp -o host_tests/test_load_angle && host_tests/test_load_angle
// ============================================================
#include "load_angle.h"
#include <cstdio>

#include "check.h"

static void test_correct() {
    // raw = W cos(boom + phi): correcting gives W back
    const float W = 1.07f, phi = 6.0f;
    for (float b = 0.0f; b <= 50.0f; b += 5.0f) {
        const float raw = W * cosf((b + phi) * LOADANG_DEG2RAD);
        CHECK_NEAR(loadAngle_correct(raw, b, phi), W, 1e-4);
    }
    // Tilt clamped at 70 deg, NaN boom treated as 0
    CHECK_NEAR(loadAngle_factor(85.0f, 0.0f), cosf(70.0f * LOADANG_DEG2RAD), 1e-6);
    CHECK_NEAR(loadAngle_factor(-85.0f, 0.0f), cosf(70.0f * LOADANG_DEG2RAD), 1e-6);
    CHECK_NEAR(loadAngle_factor(NAN, 0.0f), 1.0f, 1e-6);
}

static void test_fit_synthetic() {
    // Exact data: K = 400000 counts/kg, phi = 6 deg
    const float  b[3]  = { 5.0f, 25.0f, 45.0f };
    const float  kg[3] = { 1.07f, 1.07f, 0.5f };
    double c[3];
    for (int i = 0; i < 3; i++) c[i] = 400000.0 * kg[i] * cos((b[i] + 6.0) * (double)LOADANG_DEG2RAD);
    const LoadAngleFit f = loadAngle_fit(b, c, kg, 3);
    CHECK(f.ok);
    CHECK_NEAR(f.phiDeg, 6.0f, 0.051f);
    CHECK_NEAR(f.countsPerKg, 400000.0f, 400.0f);
    CHECK(f.worstErrG < 1.0f);
}

static void test_fit_real_lift() {
    // 1.07 kg coin box, new boom top, 9 Nov 17:51 lift: bin means of the raw
    // reading (kg at the old scale) per 5 deg bin, going up. Offline fit: 1.148 cos(b + 6).
    const float  b[9]  = { 7.5f, 12.5f, 17.5f, 22.5f, 27.5f, 32.5f, 37.5f, 42.5f, 46.0f };
    const double r[9]  = { 1.108, 1.091, 1.059, 1.017, 0.964, 0.901, 0.829, 0.763, 0.724 };
    float kg[9];
    for (int i = 0; i < 9; i++) kg[i] = 1.07f;
    const LoadAngleFit f = loadAngle_fit(b, r, kg, 9);   // "counts" = old-scale kg, still linear
    CHECK(f.ok);
    CHECK(f.phiDeg > 3.0f && f.phiDeg < 9.0f);
    CHECK(f.worstErrG < 25.0f);
    std::printf("  real lift: phi %.1f deg, K %.4f, worst %.1f g\n", f.phiDeg, f.countsPerKg, f.worstErrG);
}

static void test_fit_rejects() {
    const float  kg[2] = { 1.0f, 1.0f };
    const double c[2]  = { 1000.0, 900.0 };
    const float  near[2] = { 10.0f, 20.0f };            // only 10 deg apart
    CHECK(!loadAngle_fit(near, c, kg, 2).ok);
    const float  one[1] = { 10.0f };
    CHECK(!loadAngle_fit(one, c, kg, 1).ok);
    // Reading that GROWS with angle a lot: phi would need to be far negative -> edge -> reject
    const float  far[2] = { 0.0f, 45.0f };
    const double up[2]  = { 500.0, 1000.0 };
    CHECK(!loadAngle_fit(far, up, kg, 2).ok);
}

int main() {
    test_correct();
    test_fit_synthetic();
    test_fit_real_lift();
    test_fit_rejects();
    return check_summary("load_angle");
}
