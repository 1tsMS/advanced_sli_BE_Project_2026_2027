// ============================================================
//  SLI decision logic: load % and which motions are allowed
//
//  Pure logic (no Arduino / RTOS), unit-tested on the PC in host_tests/.
//
//  Real load moment indicators don't stop everything at 100 %: they cut only
//  the motions that make the situation worse and keep the ones that recover.
//  "Worse" is decided from the chart itself, not a hard-coded list: for each
//  axis and direction we evaluate the load % one small step ahead in that
//  direction. A direction is blocked when that step lands at or above the cut
//  threshold AND does not reduce the load %. Consequences:
//    - at 100 %, boom down / extend are cut, boom up / retract stay allowed
//      (whatever shape the chart has, including swing zones)
//    - a motion is cut one step BEFORE it reaches 100 % (predictive cut)
//    - the chart edges are soft travel limits (capacity 0 outside the chart)
//  Hoisting (winch up) is the one motion the geometry can't predict: it is cut
//  while the state is CUT.
//
//  FAULT (no valid chart, a sensor down, load reading invalid): we can't know
//  the load moment, so only the motions that always reduce it are allowed:
//  boom up, retract, winch pay out (+ boom down, see SLI_BOOM_DOWN_ALWAYS).
// ============================================================
#ifndef SAFETY_LOGIC_H
#define SAFETY_LOGIC_H

#include "load_chart.h"

enum SliState : uint8_t {
    SLI_OK    = 0,
    SLI_WARN  = 1,
    SLI_CUT   = 2,
    SLI_FAULT = 3
};

// ---- Axis / direction meaning (matches the dashboard and mega_executor) ----
// M1 swing: D1 is assumed to INCREASE the swing angle. VERIFY on the crane:
// hold Swing D1 and watch swingAngle; if it decreases, set this to -1.
#define SLI_SWING_D1_SIGN   1
// M2 boom: D1 = up (angle increases).  M3 telescope: D1 = extend.
// M4 winch: D0 = wind up / hoist (load can only rise), D1 = pay out.
#define SLI_WINCH_HOIST_DIR 0

// Boom down is never cut by the SLI, in any state (FAULT included). MS's call
// (2026-11-04): on this crane the lift limit is the motor stalling, which only
// gets easier going down. Cost: the chart's lower angle edge no longer stops the
// boom, so nothing in software keeps the leadscrew T-nut off the coupling at the
// bottom: a limit switch has to. Set to 0 to cut boom down like every other move.
#define SLI_BOOM_DOWN_ALWAYS 1

// ---- Look-ahead step per axis (how far ahead a motion is checked) ----
// At dashboard speeds the axes move well under these in the ~30 ms it takes
// the cut to reach the motor, so the cut lands before the limit.
#define SLI_LOOK_BOOM_DEG   1.0f
#define SLI_LOOK_EXT_MM     2.0f
#define SLI_LOOK_SWING_DEG  3.0f

#define SLI_HYST_PCT        2.0f     // state hysteresis (WARN/CUT clear this far below)
#define SLI_PCT_MAX         999.0f   // shown for "outside the chart" (capacity 0)
#define SLI_MAX_LOAD_KG     10.0f    // above this the reading is an error, not a load

struct SafetyInputs {
    float boomDeg;
    float extMm;
    float swingDeg;
    float loadKg;
    bool  sensorsOK;
};

/** Why motions are cut (beyond the normal load % look-ahead). */
enum SliReason : uint8_t {
    SLI_WHY_NONE      = 0,   // in the chart, sensors fine: cuts come from the look-ahead only
    SLI_WHY_NO_CHART  = 1,   // FAULT: no valid chart loaded
    SLI_WHY_SENSOR    = 2,   // FAULT: a sensor is down (SensorData::sensorFault says which)
    SLI_WHY_LOAD      = 3,   // FAULT: load reading above SLI_MAX_LOAD_KG (HX711 garbage)
    SLI_WHY_BAD_VALUE = 4,   // FAULT: an angle / extension / load is NaN
    SLI_WHY_OUTSIDE   = 5    // the pose is outside the chart: capacity 0, only moves back in
};

struct SafetyResult {
    SliState state;
    float    pct;        // load % of rated capacity (0 in FAULT)
    float    capKg;      // rated capacity at this pose (0 in FAULT / outside chart)
    int8_t   zone;       // swing zone index, -1 = none
    uint8_t  blocked;    // bit sli_bit(axis, dir) set = that motion is not allowed
    SliReason reason;
};

/** Bit for axis 1-4, direction 0/1 in SafetyResult::blocked. */
inline uint8_t sli_bit(uint8_t axis, uint8_t dir) {
    return (uint8_t)(1u << (((axis - 1u) & 3u) * 2u + (dir & 1u)));
}

/**
 * One evaluation. chart = nullptr when no valid chart is loaded.
 * prev = the state returned last time (for hysteresis).
 */
SafetyResult sli_evaluate(const LoadChart* chart, const SafetyInputs& in, SliState prev);

const char* sli_stateName(SliState s);
const char* sli_reasonName(SliReason r);

#endif // SAFETY_LOGIC_H
