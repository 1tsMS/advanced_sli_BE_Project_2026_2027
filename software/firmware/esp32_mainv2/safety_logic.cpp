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

SafetyResult sli_evaluate(const LoadChart* chart, const SafetyInputs& in, SliState prev) {
    SafetyResult r;
    r.state   = SLI_FAULT;
    r.pct     = 0.0f;
    r.capKg   = 0.0f;
    r.zone    = -1;
    r.blocked = 0;

    const bool inputsOK = in.sensorsOK &&
                          isfinite(in.boomDeg) && isfinite(in.extMm) && isfinite(in.swingDeg) &&
                          isfinite(in.loadKg) && in.loadKg <= SLI_MAX_LOAD_KG;
    if (!chart || !inputsOK) {
        // Everything except the motions that always reduce the load moment
        r.blocked = (uint8_t)~(sli_bit(2, 1) | sli_bit(3, 0) | sli_bit(4, 1 - SLI_WINCH_HOIST_DIR));
        return r;
    }

    const LoadChart& c = *chart;
    r.pct   = pctAt(c, in.loadKg, in.boomDeg, in.extMm, in.swingDeg, &r.capKg, &r.zone);
    r.state = nextState(prev, r.pct, c.warnPct, c.cutPct);

    // Geometric axes: check one step ahead in each direction
    const float swingStep = SLI_LOOK_SWING_DEG * (float)SLI_SWING_D1_SIGN;
    for (uint8_t dir = 0; dir <= 1; dir++) {
        const float s = dir ? 1.0f : -1.0f;   // D1 = +step on every axis (swing sign above)
        const float ahead[3] = {
            pctAt(c, in.loadKg, in.boomDeg, in.extMm, in.swingDeg + s * swingStep, nullptr, nullptr),
            pctAt(c, in.loadKg, in.boomDeg + s * SLI_LOOK_BOOM_DEG, in.extMm, in.swingDeg, nullptr, nullptr),
            pctAt(c, in.loadKg, in.boomDeg, in.extMm + s * SLI_LOOK_EXT_MM, in.swingDeg, nullptr, nullptr),
        };
        for (uint8_t a = 0; a < 3; a++) {
            if (ahead[a] >= c.cutPct && ahead[a] >= r.pct) r.blocked |= sli_bit((uint8_t)(a + 1), dir);
        }
    }

    // Hoisting can only add load: cut it while overloaded
    if (r.state == SLI_CUT) r.blocked |= sli_bit(4, SLI_WINCH_HOIST_DIR);
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
