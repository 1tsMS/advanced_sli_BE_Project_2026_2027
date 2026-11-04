// ============================================================
//  PC unit tests for the swing input shaper
//    mega_executor/zv_shaper.h   (queue: half now, half T/2 later)
//    esp32_mainv2/shaper_math.h  (T/2 from the pendulum length)
//
//  Build + run from software/firmware:
//    g++ -std=c++17 -Wall -Wextra -I mega_executor -I esp32_mainv2 host_tests/test_shaper.cpp -o host_tests/test_shaper && host_tests/test_shaper
// ============================================================
#include "zv_shaper.h"
#include "shaper_math.h"
#include <cmath>
#include <cstdio>
#include <initializer_list>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond) do { \
    if (cond) g_pass++; \
    else { g_fail++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_NEAR(a, b, tol) do { \
    const double _a = (a), _b = (b); \
    if (std::fabs(_a - _b) <= (tol)) g_pass++; \
    else { g_fail++; std::printf("FAIL %s:%d  %s = %.4f, expected %.4f\n", __FILE__, __LINE__, #a, _a, _b); } \
} while (0)

static void test_half_period() {
    // T/2 = pi sqrt(L/g): 150 mm -> 389 ms, 400 mm -> 634 ms
    CHECK_NEAR(shaper_halfPeriodMs(150.0f), 389, 1);
    CHECK_NEAR(shaper_halfPeriodMs(400.0f), 634, 1);
    // Length: hook drop + CG offset, override wins, clamped, NaN safe
    CHECK_NEAR(shaper_lengthMm(150.0f, 0.0f, 25.0f, 50.0f, 1500.0f), 175.0f, 1e-4);
    CHECK_NEAR(shaper_lengthMm(150.0f, 300.0f, 25.0f, 50.0f, 1500.0f), 300.0f, 1e-4);
    CHECK_NEAR(shaper_lengthMm(-80.0f, 0.0f, 25.0f, 50.0f, 1500.0f), 50.0f, 1e-4);
    CHECK_NEAR(shaper_lengthMm(9000.0f, 0.0f, 25.0f, 50.0f, 1500.0f), 1500.0f, 1e-4);
    CHECK_NEAR(shaper_lengthMm(NAN, 0.0f, 25.0f, 50.0f, 1500.0f), 50.0f, 1e-4);
}

static void test_start_and_release() {
    ZvShaper s; s.reset(); s.setHalfPeriod(400);
    s.setRef(1000.0f, 0);
    CHECK_NEAR(s.update(0),   500.0f, 1e-3);    // first half at once
    CHECK_NEAR(s.update(399), 500.0f, 1e-3);
    CHECK_NEAR(s.update(400), 1000.0f, 1e-3);   // full speed at T/2
    // held button repeats: no new event
    s.setRef(1000.0f, 500); s.setRef(1000.0f, 600);
    CHECK_NEAR(s.update(700), 1000.0f, 1e-3);
    // release: half, then stop at T/2
    s.setRef(0.0f, 1000);
    CHECK_NEAR(s.update(1000), 500.0f, 1e-3);
    CHECK_NEAR(s.update(1399), 500.0f, 1e-3);
    CHECK_NEAR(s.update(1400), 0.0f, 1e-3);
    CHECK(s.ref() == 0.0f);
}

static void test_reversal() {
    ZvShaper s; s.reset(); s.setHalfPeriod(300);
    s.setRef(800.0f, 0);  s.update(400);
    s.setRef(-800.0f, 1000);
    CHECK_NEAR(s.update(1000), 0.0f, 1e-3);     // halves cancel: pause for T/2
    CHECK_NEAR(s.update(1300), -800.0f, 1e-3);  // then the new direction, full speed
}

static void test_reset_and_overflow() {
    ZvShaper s; s.reset(); s.setHalfPeriod(500);
    s.setRef(600.0f, 0);
    s.reset();                                   // hard stop drops the pending half
    CHECK_NEAR(s.update(600), 0.0f, 1e-3);
    // more changes than the queue holds inside T/2: oldest applied early, never stuck
    for (int i = 1; i <= 12; i++) s.setRef(100.0f * i, (uint32_t)i);
    CHECK_NEAR(s.update(5000), 1200.0f, 1e-3);   // settles on the last reference
    s.setRef(0.0f, 6000);
    CHECK_NEAR(s.update(6500), 0.0f, 1e-3);
}

/**
 * Pendulum hanging from a point moving along a line at speed v(t):
 *   theta'' = -(g/L) sin(theta) - a(t)/L * cos(theta)
 * Residual sway after a move must be near zero with ZV, large without.
 */
static double residualSwayDeg(bool shaped, float lengthMm, float shaperLengthMm = 0.0f) {
    const double g = 9810.0, L = lengthMm;
    const uint32_t half = shaper_halfPeriodMs(shaperLengthMm > 0.0f ? shaperLengthMm : lengthMm);
    ZvShaper s; s.reset(); s.setHalfPeriod(half);
    const double vmax = 60.0;             // mm/s of the suspension point
    double th = 0, om = 0, vPrev = 0, maxAfter = 0;
    const double dt = 0.0005;
    for (int k = 0; k < (int)(6.0 / dt); k++) {
        const double t = k * dt;
        const uint32_t ms = (uint32_t)(t * 1000.0 + 0.5);
        const double refV = (t < 1.5) ? vmax : 0.0;          // operator holds 1.5 s
        double v;
        if (shaped) { s.setRef((float)refV, ms); v = s.update(ms); }
        else        { v = refV; }
        const double a = (v - vPrev) / dt; vPrev = v;
        const double alpha = -(g / L) * std::sin(th) - (a / L) * std::cos(th);
        om += alpha * dt; th += om * dt;
        if (t > 1.5 + half / 1000.0 + 0.05) maxAfter = std::fmax(maxAfter, std::fabs(th));
    }
    return maxAfter * 180.0 / SHAPER_PI;
}

static void test_pendulum_sim() {
    for (float L : {150.0f, 250.0f, 400.0f}) {
        const double raw = residualSwayDeg(false, L), zv = residualSwayDeg(true, L);
        std::printf("  L=%3.0f mm  residual sway unshaped %.3f deg, ZV %.4f deg\n", L, raw, zv);
        CHECK(raw > 0.5);
        CHECK(zv < 0.05 * raw);
    }
    // Shaper tuned 20 % off the real length (CG guess wrong): ZV is not robust
    // like ZVD, but must still remove most of the sway
    const double raw = residualSwayDeg(false, 250.0f);
    const double off = residualSwayDeg(true, 250.0f, 300.0f);
    std::printf("  L=250 mm, shaper tuned for 300 mm: ZV %.3f deg (%.0f %% of unshaped)\n", off, 100.0 * off / raw);
    CHECK(off < 0.4 * raw);
}

int main() {
    test_half_period();
    test_start_and_release();
    test_reversal();
    test_reset_and_overflow();
    test_pendulum_sim();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
