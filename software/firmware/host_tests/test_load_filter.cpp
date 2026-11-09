// ============================================================
//  PC unit tests for the adaptive load smoothing
//    esp32_mainv2/load_filter.h
//
//  Build + run from software/firmware:
//    g++ -std=c++17 -Wall -Wextra -I esp32_mainv2 -I host_tests host_tests/test_load_filter.cpp -o host_tests/test_load_filter && host_tests/test_load_filter
// ============================================================
#include "load_filter.h"
#include <cstdio>
#include <cstdlib>

#include "check.h"

static const float DT = 0.01f;   // SensorTask runs at 100 Hz

static void test_first_sample_and_nan() {
    LoadFilter f;
    CHECK_NEAR(f.update(1.0f, DT), 1.0f, 1e-6);         // primes on the first reading
    CHECK_NEAR(f.update(NAN, DT), 1.0f, 1e-6);          // NaN keeps the last value
}

static void test_noise_smoothed() {
    // +-12 g square-ish noise around 1.075 kg: output must stay within +-3 g
    LoadFilter f;
    f.update(1.075f, DT);
    float worst = 0.0f;
    srand(1);
    for (int i = 0; i < 3000; i++) {
        const float n = ((rand() % 2001) - 1000) / 1000.0f * 0.012f;
        const float y = f.update(1.075f + n, DT);
        if (i > 300 && fabsf(y - 1.075f) > worst) worst = fabsf(y - 1.075f);
    }
    CHECK(worst < 0.003f);
}

static void test_step_passes_fast() {
    // Real step 0 -> 1.0 kg: past 95 % within 0.35 s (3 tau_fast + one sample)
    LoadFilter f;
    f.update(0.0f, DT);
    float y = 0.0f;
    for (int i = 0; i < 35; i++) y = f.update(1.0f, DT);
    CHECK(y > 0.95f);
    // ...and back to an empty hook settles to exactly 0
    for (int i = 0; i < 300; i++) y = f.update(0.0f, DT);
    CHECK(y == 0.0f);
}

static void test_small_drift_tracks() {
    // 30 g change (inside the band) is followed with tau = 1 s: ~95 % after 3 s
    LoadFilter f;
    f.update(1.000f, DT);
    float y = 0.0f;
    for (int i = 0; i < 300; i++) y = f.update(1.030f, DT);
    CHECK(fabsf(y - 1.030f) < 0.002f);
}

int main() {
    test_first_sample_and_nan();
    test_noise_smoothed();
    test_step_passes_fast();
    test_small_drift_tracks();
    return check_summary("load_filter");
}
