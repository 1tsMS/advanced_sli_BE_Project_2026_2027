// ============================================================
//  PC unit tests for esp32_mainv2/encoder_axis.cpp (pure logic, no Arduino)
//
//  Build + run from software/firmware:
//    g++ -std=c++17 -Wall -Wextra -I esp32_mainv2 -I host_tests host_tests/test_encoder_axis.cpp esp32_mainv2/encoder_axis.cpp -o host_tests/test_encoder_axis && host_tests/test_encoder_axis
// ============================================================
#include "encoder_axis.h"
#include <cmath>
#include <cstdio>

#include "check.h"
static uint32_t T = 0;   // fake millis() clock for update()

static void test_wrapDiff() {
    CHECK(EncoderAxis::wrapDiff(10, 5) == 5);
    CHECK(EncoderAxis::wrapDiff(5, 10) == -5);
    CHECK(EncoderAxis::wrapDiff(2, 4094) == 4);      // across the 4095 -> 0 wrap
    CHECK(EncoderAxis::wrapDiff(4094, 2) == -4);
    CHECK(EncoderAxis::wrapDiff(2048, 0) == -2048);  // exactly half a turn: range is [-2048, 2047]
}

static void test_combineReads() {
    uint16_t out = 0;
    const uint16_t agree[3] = { 1000, 1002, 999 };
    CHECK(EncoderAxis::combineReads(agree, 3, 24, &out) && out == 1000);

    const uint16_t oneBad[3] = { 1000, 1000 ^ 0x800, 1001 };   // MSB flipped on one transfer
    CHECK(EncoderAxis::combineReads(oneBad, 3, 24, &out) && (out == 1000 || out == 1001));

    const uint16_t wrapBad[3] = { 4094, 1, 3000 };              // two good reads straddle the wrap
    CHECK(EncoderAxis::combineReads(wrapBad, 3, 24, &out) && (out == 4094 || out == 1));

    const uint16_t two[2] = { 500, 510 };
    CHECK(EncoderAxis::combineReads(two, 2, 24, &out) && out == 500);
    const uint16_t twoBad[2] = { 500, 900 };
    CHECK(!EncoderAxis::combineReads(twoBad, 2, 24, &out));
    CHECK(!EncoderAxis::combineReads(two, 1, 24, &out));
}

static void test_rotary() {
    EncoderAxis swing(ENC_ROTARY, 360.0f);
    swing.configure(1, 0, 0.0f, 360.0f);
    CHECK_NEAR(swing.value(), 0.0, 1e-6);                 // no sample yet
    swing.update(1024, T += 10);
    CHECK_NEAR(swing.value(), 90.0, 0.01);
    swing.update(3072, T += 10);                               // absolute: any jump is fine
    CHECK_NEAR(swing.value(), -90.0, 0.01);               // wrapped to +/-180

    swing.update(1024, T += 10);
    swing.setReference(0.0f);                             // "this is home"
    CHECK_NEAR(swing.value(), 0.0, 1e-4);
    swing.update(1024 + 512, T += 10);
    CHECK_NEAR(swing.value(), 45.0, 0.01);
    swing.setInvert(true);                                // mirrors around home
    CHECK_NEAR(swing.value(), -45.0, 0.01);
    CHECK(swing.zeroRaw() == 1024);

    // Boom zeroed at a known angle, across the raw wrap
    EncoderAxis boom(ENC_ROTARY, 360.0f);
    boom.update(4000, T += 10);
    boom.setReference(5.0f);                              // boom rests at 5 deg
    boom.update(100, T += 10);                                 // +196 counts across the wrap
    CHECK_NEAR(boom.value(), 5.0 + 196 * 360.0 / 4096, 0.01);

    // Calibration restored from flash after a reboot
    EncoderAxis reboot(ENC_ROTARY, 360.0f);
    reboot.configure(-1, 4000, 5.0f, 360.0f);
    reboot.update(100, T += 10);
    CHECK_NEAR(reboot.value(), 5.0 - 196 * 360.0 / 4096, 0.01);
}

static void test_linear_multiturn() {
    const float scale = 19.048f;
    EncoderAxis tele(ENC_LINEAR, scale);
    tele.configure(1, 0, 0.0f, scale);
    uint16_t raw = 3000;
    tele.update(raw, T += 10);
    CHECK_NEAR(tele.value(), 0.0, 1e-6);                  // starts at 0 at power-up

    for (int i = 0; i < 4096 * 10 / 256; i++) {           // 10 turns in 256-count steps
        raw = (uint16_t)((raw + 256) % 4096);
        CHECK(tele.update(raw, T += 10));
    }
    CHECK_NEAR(tele.value(), 10.0 * scale, 0.01);
    CHECK(tele.countsFromReference() == 40960);

    for (int i = 0; i < 4096 * 4 / 256; i++) {            // 4 turns back
        raw = (uint16_t)((raw + 4096 - 256) % 4096);
        tele.update(raw, T += 10);
    }
    CHECK_NEAR(tele.value(), 6.0 * scale, 0.01);

    tele.setReference(0.0f);
    CHECK_NEAR(tele.value(), 0.0, 1e-4);
    tele.setUnitsPerRev(8.0f);
    raw = (uint16_t)((raw + 1024) % 4096);
    CHECK(tele.update(raw, T += 30));                          // quarter turn in 30 ms is plausible
    CHECK_NEAR(tele.value(), 2.0, 1e-4);                  // quarter turn at 8 mm/rev
    tele.setInvert(true);
    CHECK_NEAR(tele.value(), -2.0, 1e-4);
}

static void test_rate_aware_gate() {
    // The old firmware rejected anything over 45 deg (512 counts) per sample,
    // assuming 10 ms. A 100 ms stall made real motion look like a glitch.
    EncoderAxis tele(ENC_LINEAR, 360.0f);
    uint16_t raw = 0;
    tele.update(raw, T += 10);

    raw = 900;
    CHECK(!tele.update(raw, T += 10));                         // 900 counts in 10 ms: glitch
    CHECK(tele.rejected() == 1);
    CHECK(tele.update(raw, T += 100));                         // same jump after a 100 ms gap: real
    CHECK_NEAR(tele.value(), 900 * 360.0 / 4096, 0.01);

    // One corrupted sample is dropped and does not shift the count
    const double before = tele.value();
    CHECK(!tele.update((uint16_t)((raw + 2000) % 4096), T += 10));
    CHECK(tele.update((uint16_t)(raw + 20), T += 10));
    CHECK_NEAR(tele.value(), before + 20 * 360.0 / 4096, 0.01);
}

static void test_resync() {
    EncoderAxis tele(ENC_LINEAR, 360.0f);
    tele.update(0, T += 10);
    // Shaft jumped by more than the direction-safe limit (e.g. encoder unplugged and
    // replugged): after RESYNC_AFTER rejections the new position is taken without
    // counting the jump.
    for (int i = 0; i < EncoderAxis::RESYNC_AFTER - 1; i++) CHECK(!tele.update(2000, T += 10));
    CHECK(tele.update(2000, T += 10));
    CHECK(tele.resyncs() == 1);
    CHECK_NEAR(tele.value(), 0.0, 1e-6);                  // jump not counted
    CHECK(tele.update(2010, T += 10));
    CHECK_NEAR(tele.value(), 10 * 360.0 / 4096, 0.01);    // tracks normally afterwards
}

static void test_failed_reads_during_motion() {
    // Shaft turns 300 counts per 10 ms. Reads fail for 3 cycles (no update() calls),
    // then a good sample arrives covering 40 ms of motion = 1200 counts.
    // The old gate used the loop period (10 ms -> limit 510) and threw it away.
    EncoderAxis tele(ENC_LINEAR, 360.0f);
    uint16_t raw = 0;
    tele.update(raw, T += 10);
    raw = 300;  CHECK(tele.update(raw, T += 10));
    T += 30;                                              // three failed reads
    raw = 1500; CHECK(tele.update(raw, T += 10));
    CHECK(tele.rejected() == 0);
    CHECK(tele.countsFromReference() == 1500);
}

static void test_restore_keeps_zero() {
    const float scale = 19.048f;
    EncoderAxis a(ENC_LINEAR, scale);
    a.update(100, T += 10);
    a.setReference(0.0f);                                 // retracted = 0
    uint16_t raw = 100;
    for (int i = 0; i < 40; i++) { raw = (uint16_t)((raw + 300) % 4096); a.update(raw, T += 10); }
    const double before = a.value();                      // ~55.8 mm out
    const int32_t savedCounts = a.rawCountsFromReference();
    const uint16_t savedRaw = a.lastRaw();

    // Reboot: shaft drifted 50 counts while off
    EncoderAxis b(ENC_LINEAR, scale);
    b.update((uint16_t)((savedRaw + 50) % 4096), T += 10);
    b.restore(0.0f, savedCounts, savedRaw);
    CHECK_NEAR(b.value(), before + 50 * scale / 4096, 1e-3);

    // Invert after a reboot must mirror around the TRUE zero (retracted), not the boot position
    b.setInvert(true);
    CHECK_NEAR(b.value(), -(before + 50 * scale / 4096), 1e-3);
    b.setInvert(false);
    // Scale change after a reboot is also measured from the true zero
    b.setUnitsPerRev(2 * scale);
    CHECK_NEAR(b.value(), 2 * (before + 50 * scale / 4096), 1e-3);
}

int main() {
    test_wrapDiff();
    test_combineReads();
    test_rotary();
    test_linear_multiturn();
    test_rate_aware_gate();
    test_resync();
    test_failed_reads_during_motion();
    test_restore_keeps_zero();
    return check_summary("encoder_axis");
}
