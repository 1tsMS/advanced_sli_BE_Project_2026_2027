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

uint8_t sli_travelLimits(const SafetyInputs& in) {
    uint8_t m = 0;
    if (isfinite(in.boomDeg) && in.boomDeg <= SLI_BOOM_MIN_DEG) m |= sli_bit(2, 0);   // boom down
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
        // Everything except the motions that always reduce the load moment
        r.blocked = (uint8_t)~(sli_bit(2, 1) | sli_bit(3, 0) | sli_bit(4, 1 - SLI_WINCH_HOIST_DIR));
        if (SLI_BOOM_DOWN_ALWAYS) r.blocked &= (uint8_t)~sli_bit(2, 0);
        r.blocked |= sli_travelLimits(in);
        return r;
    }

    const LoadChart& c = *chart;
    r.pct   = pctAt(c, in.loadKg, in.boomDeg, in.extMm, in.swingDeg, &r.capKg, &r.zone);
    r.state = nextState(prev, r.pct, c.warnPct, c.cutPct);
    if (!(r.capKg > 0.0f)) r.reason = SLI_WHY_OUTSIDE;

    // Geometric axes: check one step ahead in each direction.
    // D1 = +step on every axis (for swing, D1 moves the angle by SLI_SWING_D1_SIGN).
    const float baseStep[3] = { SLI_LOOK_SWING_DEG, SLI_LOOK_BOOM_DEG, SLI_LOOK_EXT_MM };
    const float rateD1[3]   = { in.swingRate * (float)SLI_SWING_D1_SIGN, in.boomRate, in.extRate };
    for (uint8_t dir = 0; dir <= 1; dir++) {
        const float s = dir ? 1.0f : -1.0f;
        float step[3];
        for (uint8_t a = 0; a < 3; a++) {
            step[a] = baseStep[a];
            const float towards = s * rateD1[a];          // speed in THIS direction (> 0 = moving that way)
            if (in.cutMode == SLI_CUT_SPEED && isfinite(towards) && towards * SLI_LOOK_TIME_S > step[a]) {
                step[a] = towards * SLI_LOOK_TIME_S;
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
    r.blocked |= sli_travelLimits(in);
    if (r.pct > SLI_PCT_MAX) r.pct = SLI_PCT_MAX;   // the distance term is internal only
    return r;
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
