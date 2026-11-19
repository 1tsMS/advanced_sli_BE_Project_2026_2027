// ============================================================
//  SLI decision logic — Implementation (pure logic)
// ============================================================
#include "safety_logic.h"
#include <math.h>

/**
 * Load % at a pose. Outside the chart it is SLI_PCT_MAX PLUS the distance
 * outside, so a step back toward the chart always counts as an improvement
 * (otherwise a boom parked a few degrees below the chart would be stuck, with
 * every direction reading the same 999 %).
 */
static float pctAt(const LoadChart& c, float load, float boom, float ext, float swing,
                   float* capOut, int8_t* zoneOut) {
    const float cap = lc_capacity(c, boom, ext, swing, zoneOut);
    if (capOut) *capOut = cap;
    if (!(cap > 0.0f)) return SLI_PCT_MAX + lc_outsideBy(c, boom, ext);
    const float pct = (load > 0.0f ? load : 0.0f) / cap * 100.0f;
    return pct < SLI_PCT_MAX ? pct : SLI_PCT_MAX;
}

static SliState nextState(SliState prev, float pct, float warn, float cut) {
    if (pct >= cut)                                            return SLI_CUT;
    if (prev == SLI_CUT && pct >= cut - SLI_HYST_PCT)          return SLI_CUT;
    if (pct >= warn)                                           return SLI_WARN;
    if ((prev == SLI_WARN || prev == SLI_CUT) &&
        pct >= warn - SLI_HYST_PCT)                            return SLI_WARN;
    return SLI_OK;
}

/**
 * Is moving the boom UP safe everywhere on this chart, i.e. capacity never falls
 * as the angle rises (real cranes: the radius shrinks)? On this crane the chart
 * encodes the boom motor's stall limit and capacity FALLS with angle, so boom up
 * is the critical direction and must not be unlocked when the pose is unknown.
 */
static bool boomUpNeverWorse(const LoadChart& c) {
    for (uint8_t j = 0; j < c.nE; j++)
        for (uint8_t i = 0; i + 1 < c.nA; i++)
            if (c.capKg[i + 1][j] < c.capKg[i][j]) return false;
    return true;
}

/** Same for retracting: capacity never falls as the extension gets shorter. */
static bool retractNeverWorse(const LoadChart& c) {
    for (uint8_t i = 0; i < c.nA; i++)
        for (uint8_t j = 0; j + 1 < c.nE; j++)
            if (c.capKg[i][j] < c.capKg[i][j + 1]) return false;
    return true;
}

uint8_t sli_travelLimits(const SafetyInputs& in, const LoadChart* chart) {
    const float boomMin = chart ? chart->boomMinDeg : SLI_BOOM_MIN_DEG;
    uint8_t m = 0;
    if (isfinite(in.boomDeg) && in.boomDeg <= boomMin) m |= sli_bit(2, 0);              // boom down
    if (chart && isfinite(in.extMm) && in.extMm <= chart->extMinMm) m |= sli_bit(3, 0); // retract
    return m;
}

SafetyResult sli_evaluate(const LoadChart* chart, const SafetyInputs& in, SliState prev) {
    SafetyResult r;
    r.state   = SLI_FAULT;
    r.pct     = 0.0f;
    r.capKg   = 0.0f;
    r.zone    = -1;
    r.blocked = 0;
    r.reason  = SLI_WHY_NONE;

    const bool finite = isfinite(in.boomDeg) && isfinite(in.extMm) && isfinite(in.swingDeg) &&
                        isfinite(in.loadKg);
    if      (!chart)                         r.reason = SLI_WHY_NO_CHART;
    else if (!in.sensorsOK)                  r.reason = SLI_WHY_SENSOR;
    else if (!finite)                        r.reason = SLI_WHY_BAD_VALUE;
    else if (in.loadKg > SLI_MAX_LOAD_KG)    r.reason = SLI_WHY_LOAD;
    if (r.reason != SLI_WHY_NONE) {
        // Pose or load unknown: allow only the motions that can never make it worse.
        // Paying out never adds load. Boom up / retract only when the chart itself
        // says so everywhere (no chart: boom up stays cut, retract always shortens
        // the radius). A hard-coded "boom up is safe" let the boom climb past the
        // limit on this crane, whose capacity falls with angle (MS, 18 Nov).
        uint8_t allow = sli_bit(4, 1 - SLI_WINCH_HOIST_DIR);
        if (chart ? retractNeverWorse(*chart) : true) allow |= sli_bit(3, 0);
        if (chart && boomUpNeverWorse(*chart))        allow |= sli_bit(2, 1);
        r.blocked = (uint8_t)~allow;
        if (SLI_BOOM_DOWN_ALWAYS) r.blocked &= (uint8_t)~sli_bit(2, 0);
        r.blocked |= sli_travelLimits(in, chart);
        return r;
    }

    const LoadChart& c = *chart;
    r.pct   = pctAt(c, in.loadKg, in.boomDeg, in.extMm, in.swingDeg, &r.capKg, &r.zone);
    r.state = nextState(prev, r.pct, c.warnPct, c.cutPct);
    if (!(r.capKg > 0.0f)) r.reason = SLI_WHY_OUTSIDE;

    // Geometric axes: check one step ahead in each direction.
    // D1 = +step on every axis (for swing, D1 moves the angle by SLI_SWING_D1_SIGN).
    const float baseStep[3] = { SLI_LOOK_SWING_DEG, SLI_LOOK_BOOM_DEG, SLI_LOOK_EXT_MM };
    const float margin[3]   = { SLI_SPEED_MARGIN_SWING_DEG, SLI_SPEED_MARGIN_BOOM_DEG, SLI_SPEED_MARGIN_EXT_MM };
    const float rateD1[3]   = { in.swingRate * (float)SLI_SWING_D1_SIGN, in.boomRate, in.extRate };
    for (uint8_t dir = 0; dir <= 1; dir++) {
        const float s = dir ? 1.0f : -1.0f;
        float step[3];
        for (uint8_t a = 0; a < 3; a++) {
            step[a] = baseStep[a];
            if (in.cutMode == SLI_CUT_SPEED) {
                // Protective distance, ISO/TS 15066 form: uncertainty + speed x stop time.
                // Moving away (or still) leaves only the margin.
                const float towards = s * rateD1[a];      // speed in THIS direction (> 0 = moving that way)
                step[a] = margin[a] + ((isfinite(towards) && towards > 0.0f) ? towards * SLI_LOOK_TIME_S : 0.0f);
            }
        }
        const float swingDelta = s * step[0] * (float)SLI_SWING_D1_SIGN;
        const float ahead[3] = {
            pctAt(c, in.loadKg, in.boomDeg, in.extMm, in.swingDeg + swingDelta, nullptr, nullptr),
            pctAt(c, in.loadKg, in.boomDeg + s * step[1], in.extMm, in.swingDeg, nullptr, nullptr),
            pctAt(c, in.loadKg, in.boomDeg, in.extMm + s * step[2], in.swingDeg, nullptr, nullptr),
        };
        // REACTIVE waits until the limit is already reached; the others cut the
        // step that would reach it. Either way only a step that doesn't lower the % is cut.
        const float trigger = (in.cutMode == SLI_CUT_REACTIVE) ? r.pct : 0.0f;
        for (uint8_t a = 0; a < 3; a++) {
            const float judged = (in.cutMode == SLI_CUT_REACTIVE) ? trigger : ahead[a];
            if (judged >= c.cutPct && ahead[a] >= r.pct) r.blocked |= sli_bit((uint8_t)(a + 1), dir);
        }
    }

    // Hoisting can only add load: cut it while overloaded
    if (r.state == SLI_CUT) r.blocked |= sli_bit(4, SLI_WINCH_HOIST_DIR);
    if (SLI_BOOM_DOWN_ALWAYS) r.blocked &= (uint8_t)~sli_bit(2, 0);
    r.blocked |= sli_travelLimits(in, chart);
    if (r.pct > SLI_PCT_MAX) r.pct = SLI_PCT_MAX;   // the distance term is internal only
    return r;
}

uint8_t sli_faultBlocked(uint8_t strict, uint8_t lastValidBlocked, SliState lastValidState,
                         uint32_t msSinceValid, bool poseOnly) {
    if (poseOnly && lastValidState == SLI_OK && msSinceValid <= SLI_FAULT_GRACE_MS) return lastValidBlocked;
    return (uint8_t)(strict | lastValidBlocked);
}

const char* sli_stateName(SliState s) {
    switch (s) {
        case SLI_OK:    return "OK";
        case SLI_WARN:  return "WARN";
        case SLI_CUT:   return "CUT";
        default:        return "FAULT";
    }
}

char sli_cutModeLetter(SliCutMode m) {
    switch (m) {
        case SLI_CUT_REACTIVE: return 'R';
        case SLI_CUT_SPEED:    return 'S';
        default:               return 'P';
    }
}

const char* sli_reasonName(SliReason r) {
    switch (r) {
        case SLI_WHY_NO_CHART:  return "NO_CHART";
        case SLI_WHY_SENSOR:    return "SENSOR";
        case SLI_WHY_LOAD:      return "LOAD_RANGE";
        case SLI_WHY_BAD_VALUE: return "BAD_VALUE";
        case SLI_WHY_OUTSIDE:   return "OUTSIDE";
        default:                return "-";
    }
}
