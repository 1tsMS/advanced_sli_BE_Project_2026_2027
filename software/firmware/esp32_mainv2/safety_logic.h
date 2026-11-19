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
//  Cut modes (SLI CUT P|R|S, for the predictive-vs-reactive experiment; every
//  boot starts in PREDICTIVE):
//    PREDICTIVE  fixed look-ahead step per axis (SLI_LOOK_*)
//    REACTIVE    no look-ahead: a direction is cut only once the load % is
//                already at or above the cut threshold (classic limiter)
//    SPEED       look-ahead = margin + axis speed x SLI_LOOK_TIME_S (ISO/TS 15066
//                protective-distance form): fast stops earlier, slow gets closer
//  Hoisting (winch up) is the one motion the geometry can't predict: it is cut
//  while the state is CUT.
//
//  FAULT (no valid chart, a sensor down, load reading invalid): we can't know
//  the load moment, so only the motions that can never make it worse are allowed:
//  winch pay out, retract (unless the chart says otherwise), boom up ONLY if the
//  chart's capacity never falls with angle (+ boom down, see SLI_BOOM_DOWN_ALWAYS).
//  SafetyTask also keeps every direction the last valid evaluation had cut.
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

// ---- Travel limits: mechanical end stops, enforced while the SLI is armed ----
// BYPASS and OFF lift them too (MS, 16 Nov), so the boom can be driven onto its
// stop to re-zero the IMU.
// Boom down below this angle drives the leadscrew T-nut into the coupling: the
// boom rests on its stop at 0° (IMU zeroed there), so -0.5° = "already on the
// stop". A microswitch is still the real fix; this is the software guard.
#define SLI_BOOM_MIN_DEG   -0.5f

// ---- Look-ahead step per axis (how far ahead a motion is checked) ----
// At dashboard speeds the axes move well under these in the ~30 ms it takes
// the cut to reach the motor, so the cut lands before the limit.
#define SLI_LOOK_BOOM_DEG   1.0f
#define SLI_LOOK_EXT_MM     2.0f
#define SLI_LOOK_SWING_DEG  3.0f
// SPEED mode: look-ahead = margin + speed x SLI_LOOK_TIME_S, the protective-distance
// form of ISO/TS 15066 speed-and-separation monitoring (uncertainty + v x stop time).
// The margin is the measurement uncertainty (E2: IMU within 0.2-0.5 deg of the
// reference), much smaller than PREDICTIVE's fixed step, so a slow approach gets
// closer to the limit than PREDICTIVE and a fast one stops earlier. The time covers
// the cut path (SafetyTask 20 ms + command + UART + Mega, ~30 ms), the IMU filter and
// the 0.1 s rate smoothing, with margin. At the crane's 1.8 deg/s boom speed it looks
// ~1.15 deg ahead (about PREDICTIVE), 0.4 deg at 0.3 deg/s, 2.25 deg at 4 deg/s.
// (Was max(fixed step, v x 0.25 s): at real boom speeds it never left the fixed step.)
#define SLI_LOOK_TIME_S             0.5f
#define SLI_SPEED_MARGIN_BOOM_DEG   0.25f
#define SLI_SPEED_MARGIN_EXT_MM     0.5f
#define SLI_SPEED_MARGIN_SWING_DEG  1.0f

enum SliCutMode : uint8_t {
    SLI_CUT_PREDICTIVE = 0,
    SLI_CUT_REACTIVE   = 1,
    SLI_CUT_SPEED      = 2
};

#define SLI_HYST_PCT        2.0f     // state hysteresis (WARN/CUT clear this far below)
#define SLI_PCT_MAX         999.0f   // shown for "outside the chart" (capacity 0)
#define SLI_MAX_LOAD_KG     10.0f    // above this the reading is an error, not a load

struct SafetyInputs {
    float boomDeg;
    float extMm;
    float swingDeg;
    float loadKg;
    bool  sensorsOK;
    // Measured axis speeds (deg/s, mm/s, deg/s; + = angle / extension rising).
    // Only SPEED mode uses them; 0 = standing still or unknown.
    float boomRate;
    float extRate;
    float swingRate;
    SliCutMode cutMode;
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

/** Motions past a mechanical end stop (bits as in SafetyResult::blocked). Armed only. */
/** chart = nullptr: built-in boom stop SLI_BOOM_MIN_DEG only; else the chart's boomMinDeg / extMinMm. */
uint8_t sli_travelLimits(const SafetyInputs& in, const LoadChart* chart);

/**
 * One evaluation. chart = nullptr when no valid chart is loaded.
 * prev = the state returned last time (for hysteresis).
 */
SafetyResult sli_evaluate(const LoadChart* chart, const SafetyInputs& in, SliState prev);

// FAULT grace (MS, 18 Nov): a short POSE dropout (IMU / mutex) while the last valid
// state was OK keeps the last valid decision instead of stopping the crane. From
// OK the load % is below WARN, and at the crane's ~1.8 deg/s the boom moves about
// 5 deg in 3 s, which can't reach CUT on its charts. A load-cell fault, WARN/CUT
// before the dropout, or a dropout longer than this: strict FAULT.
#define SLI_FAULT_GRACE_MS  3000u

/**
 * Blocked mask while in FAULT. strict = sli_evaluate's FAULT mask; lastValid* =
 * the last non-FAULT evaluation; poseOnly = only the pose is unknown (load fine).
 * Never frees a direction the last valid evaluation had cut.
 */
uint8_t sli_faultBlocked(uint8_t strict, uint8_t lastValidBlocked, SliState lastValidState,
                         uint32_t msSinceValid, bool poseOnly);

const char* sli_stateName(SliState s);
const char* sli_reasonName(SliReason r);
/** "P" / "R" / "S" for the $S line and the SLI CUT command. */
char sli_cutModeLetter(SliCutMode m);

#endif // SAFETY_LOGIC_H
