// ============================================================
//  Swing input shaping: pendulum half period for the Mega's ZV shaper
//
//  Pure math (no Arduino), unit-tested in host_tests/.
//  The hanging load is a simple pendulum of length L, so its period is
//  T = 2 pi sqrt(L / g) and the ZV shaper's second impulse comes at T/2.
// ============================================================
#ifndef SHAPER_MATH_H
#define SHAPER_MATH_H

#include <math.h>
#include <stdint.h>

#define SHAPER_G_MM_S2  9810.0f
#define SHAPER_PI       3.14159265f   // M_PI is not standard C++

/** Pendulum length used for shaping: the override if given, else hook drop + load CG offset, clamped. */
inline float shaper_lengthMm(float hookDropMm, float overrideMm, float cgMm, float minMm, float maxMm) {
    float L = (overrideMm > 0.0f) ? overrideMm : hookDropMm + cgMm;
    if (!(L == L)) L = minMm;          // NaN (rope never referenced)
    if (L < minMm) L = minMm;
    if (L > maxMm) L = maxMm;
    return L;
}

/** T/2 in ms for a pendulum of length lengthMm. */
inline uint16_t shaper_halfPeriodMs(float lengthMm) {
    return (uint16_t)(1000.0f * SHAPER_PI * sqrtf(lengthMm / SHAPER_G_MM_S2) + 0.5f);
}

#endif // SHAPER_MATH_H
