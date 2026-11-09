// ============================================================
//  Load smoothing: adaptive exponential average (pure maths, host-tested)
//
//  The load on the hook changes slowly, but the HX711 reading (and more so
//  the angle-corrected one, which divides by cos(boom + phi)) carries a few
//  grams of noise. A plain heavy EMA would hide it, but it would also delay a
//  real load step, and SafetyTask cuts on this value. So the gain adapts:
//
//    |x - y| >  band : switch to tracking (tau_fast) - a real change
//    tracking        : stay fast until |x - y| < settle, so a step lands fully
//    otherwise       : y += a_slow * (x - y)   (tau_slow, smooth display)
//
//  a = dt / (tau + dt), so the result does not depend on how often it runs.
//  Measured 9 Nov: static noise 1.4-6 g sd, 23 g peak-to-peak at 43 deg,
//  so a 40 g band holds the noise and lets any real step through.
// ============================================================
#ifndef LOAD_FILTER_H
#define LOAD_FILTER_H

#include <math.h>

struct LoadFilter {
    float tauSlowS = 1.0f;     // inside the band
    float tauFastS = 0.10f;    // outside the band (about one HX711 sample)
    float bandKg   = 0.040f;   // bigger jump than this = real change
    float settleKg = 0.005f;   // tracking ends once within this
    float y        = 0.0f;
    bool  primed   = false;
    bool  tracking = false;

    void reset() { primed = false; tracking = false; }

    /** Feed one reading (kg) taken dtS seconds after the previous one; returns the smoothed load. */
    float update(float x, float dtS) {
        if (!isfinite(x)) return y;              // keep the last good value
        if (!primed || !(dtS > 0.0f)) { y = x; primed = true; return y; }
        const float e = x - y;
        if (fabsf(e) > bandKg)        tracking = true;
        else if (fabsf(e) < settleKg) tracking = false;
        const float tau = tracking ? tauFastS : tauSlowS;
        y += e * (dtS / (tau + dtS));
        if (x == 0.0f && fabsf(y) < 0.0005f) y = 0.0f;   // settle to a clean 0 on an empty hook
        return y;
    }
};

#endif // LOAD_FILTER_H
