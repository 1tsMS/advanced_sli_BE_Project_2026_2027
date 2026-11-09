// ============================================================
//  Load-cell boom-angle correction (pure maths, host-tested)
//
//  The load cell is bolted to the boom tip, so it tilts with the boom while
//  the rope always hangs vertically. A bar cell only measures the force
//  across its beam, so it reads
//
//      raw = W * cos(boom + phi)
//
//  phi is the mounting offset of the cell (beam not exactly along the boom).
//  The correction divides that back out:  W = raw / cos(boom + phi).
//
//  Calibration (CAL9): hang a known mass, capture it at 2+ boom angles at
//  least LOADANG_MIN_SPAN_DEG apart. For each candidate phi the best scale
//  K (counts per kg with the cell square to the rope) is a closed-form
//  least squares; phi is found by a 0.1 deg grid search over +-30 deg.
// ============================================================
#ifndef LOAD_ANGLE_H
#define LOAD_ANGLE_H

#include <math.h>
#include <stdint.h>

static const float LOADANG_DEG2RAD      = 0.017453292519943f;
static const float LOADANG_MAX_TILT_DEG = 70.0f;   // beyond this 1/cos amplifies noise > 2.9x
static const float LOADANG_PHI_LIMIT    = 30.0f;   // search / accept range for phi
static const float LOADANG_MIN_SPAN_DEG = 15.0f;   // captured angles must spread at least this

/** cos(boom + phi), with the tilt clamped so the divisor never goes below cos(70 deg). */
inline float loadAngle_factor(float boomDeg, float phiDeg) {
    float tilt = boomDeg + phiDeg;
    if (!isfinite(tilt)) tilt = 0.0f;
    if (tilt >  LOADANG_MAX_TILT_DEG) tilt =  LOADANG_MAX_TILT_DEG;
    if (tilt < -LOADANG_MAX_TILT_DEG) tilt = -LOADANG_MAX_TILT_DEG;
    return cosf(tilt * LOADANG_DEG2RAD);
}

/** Angle-corrected load (kg) from the raw reading (kg, tare + scale applied). */
inline float loadAngle_correct(float rawKg, float boomDeg, float phiDeg) {
    return rawKg / loadAngle_factor(boomDeg, phiDeg);
}

struct LoadAngleFit {
    bool  ok;
    float phiDeg;       // mounting offset
    float countsPerKg;  // scale with the cell square to the rope
    float worstErrG;    // worst point error after the fit
};

/**
 * Fit raw counts c_i = K * w_i * cos(boom_i + phi) over n points.
 * Fails (ok = false) with < 2 points, too small an angle spread, or a
 * phi on the edge of the search range (model does not fit).
 */
inline LoadAngleFit loadAngle_fit(const float* boomDeg, const double* counts,
                                  const float* kg, uint8_t n) {
    LoadAngleFit best = { false, 0.0f, 0.0f, 0.0f };
    if (n < 2) return best;
    float lo = boomDeg[0], hi = boomDeg[0];
    for (uint8_t i = 1; i < n; i++) {
        if (boomDeg[i] < lo) lo = boomDeg[i];
        if (boomDeg[i] > hi) hi = boomDeg[i];
    }
    if (hi - lo < LOADANG_MIN_SPAN_DEG) return best;

    double bestSse = -1.0;
    for (int p10 = (int)(-LOADANG_PHI_LIMIT * 10); p10 <= (int)(LOADANG_PHI_LIMIT * 10); p10++) {
        const float phi = p10 / 10.0f;
        double sxy = 0.0, sxx = 0.0;
        for (uint8_t i = 0; i < n; i++) {
            const double x = (double)kg[i] * loadAngle_factor(boomDeg[i], phi);
            sxy += counts[i] * x;
            sxx += x * x;
        }
        if (sxx <= 0.0) continue;
        const double K = sxy / sxx;
        double sse = 0.0;
        for (uint8_t i = 0; i < n; i++) {
            const double e = counts[i] - K * kg[i] * loadAngle_factor(boomDeg[i], phi);
            sse += e * e;
        }
        if (bestSse < 0.0 || sse < bestSse) {
            bestSse = sse;
            best.phiDeg = phi;
            best.countsPerKg = (float)K;
        }
    }
    if (bestSse < 0.0 || best.countsPerKg == 0.0f) return best;
    if (fabsf(best.phiDeg) >= LOADANG_PHI_LIMIT - 0.05f) return best;   // pinned to the edge

    float worst = 0.0f;
    for (uint8_t i = 0; i < n; i++) {
        const float w = (float)(counts[i] / best.countsPerKg) / loadAngle_factor(boomDeg[i], best.phiDeg);
        const float eG = fabsf(w - kg[i]) * 1000.0f;
        if (eG > worst) worst = eG;
    }
    best.worstErrG = worst;
    best.ok = true;
    return best;
}

#endif // LOAD_ANGLE_H
