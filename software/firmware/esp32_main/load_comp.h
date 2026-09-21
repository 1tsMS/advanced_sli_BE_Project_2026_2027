// ============================================================
//  Load Compensation — boom-angle correction for the load cell
//
//  The load cell sits on top of the boom with the cable running over it,
//  so the force it feels depends on the boom angle (and on how the cell is
//  mounted). Rather than assume a formula, the correction is learned:
//  hang a KNOWN weight, record it at a boom angle (CAL3 W<kg>) and the
//  ESP32 stores  gain = measured / known  for that angle. Between recorded
//  angles the gain is interpolated linearly; outside them the nearest
//  point is used. True load = measured / gain(angle).
//
//  With no points recorded the gain is 1.0, i.e. no correction.
//
//  Thread safety: like the load chart, the table is swapped under
//  sensorMutex, and readers (SafetyTask) call correct() while holding it.
// ============================================================
#ifndef LOAD_COMP_H
#define LOAD_COMP_H

#include "config.h"

#define LCOMP_MAX_POINTS      8
#define LCOMP_MERGE_DEG       2.0f    // recording within this of an existing point replaces it
#define LCOMP_GAIN_MIN        0.2f    // reject implausible gains (bad known weight / unloaded cell)
#define LCOMP_GAIN_MAX        5.0f
#define LCOMP_MIN_KNOWN_KG    0.05f
#define LCOMP_MAX_KNOWN_KG    100.0f
#define LCOMP_MIN_MEASURED_KG 0.02f

struct GainPoint {
    float angle;   // boom angle the weight was recorded at (deg)
    float gain;    // measured / known at that angle
};

enum LoadCompStatus : uint8_t {
    LCOMP_OK = 0,
    LCOMP_ERR_KNOWN_RANGE,   // known weight outside 0.05..100 kg
    LCOMP_ERR_NO_LOAD,       // load cell reads ~0: nothing hanging, or not tared
    LCOMP_ERR_GAIN_RANGE,    // measured/known outside 0.2..5: wrong weight entered?
    LCOMP_ERR_FULL,          // LCOMP_MAX_POINTS already used
    LCOMP_ERR_FLASH,         // NVS write failed
    LCOMP_ERR_BUSY           // could not take sensorMutex
};

class LoadCompensation {
public:
    LoadCompensation() : _count(0) {}

    /** Restore saved points from NVS. Call once in setup(). */
    void loadFromFlash();

    /** Correction factor for a boom angle (1.0 when nothing is recorded). */
    float gainAt(float angleDeg) const;

    /** True load = measured / gain(angle). */
    float correct(float measuredKg, float angleDeg) const;

    /** Record (or update) the gain at this angle and save to flash. */
    LoadCompStatus record(float angleDeg, float measuredKg, float knownKg);

    /** Remove every point (back to no correction) and save. */
    LoadCompStatus clear();

    uint8_t count() const { return _count; }
    bool get(uint8_t i, GainPoint* out) const;

    static const char* statusName(LoadCompStatus s);

private:
    GainPoint _pts[LCOMP_MAX_POINTS];
    uint8_t   _count;

    LoadCompStatus commit(const GainPoint* pts, uint8_t n);
};

#endif // LOAD_COMP_H
