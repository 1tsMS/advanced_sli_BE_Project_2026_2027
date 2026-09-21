// ============================================================
//  Safety Evaluation — Implementation
// ============================================================
#include "safety_eval.h"
#include <math.h>

SafetyOutputs safetyEvaluate(const SafetyInputs& in, SafetyState& state,
                             const LoadChart& chart, const LoadCompensation& comp) {
    SafetyOutputs out;

    out.actualLoadKg  = comp.correct(in.measuredKg, in.boomAngle);
    out.safeLoadLimit = chart.hasChart()
        ? chart.lookupLimit(in.boomAngle, in.extensionMM)
        : SAFE_LOAD_DEFAULT_KG;

    // On a small crane anything above MAX_VALID_LOAD_KG is an uncalibrated or broken
    // load cell, not a real load.
    const bool loadValid = in.loadSensorOK &&
                           in.measuredKg >= -0.5f && in.measuredKg <= MAX_VALID_LOAD_KG;

    out.statusFlags = 0;
    if (!loadValid)         out.statusFlags |= STATUS_LOAD_FAULT;
    if (!in.boomSensorOK)   out.statusFlags |= STATUS_BOOM_FAULT;
    if (!in.extSensorOK)    out.statusFlags |= STATUS_EXT_FAULT;
    if (in.boomMismatch)    out.statusFlags |= STATUS_BOOM_MISMATCH;
    if (!chart.hasChart())  out.statusFlags |= STATUS_NO_CHART;
    else if (out.safeLoadLimit <= 0.0f) out.statusFlags |= STATUS_OUT_OF_CHART;
    if (fabsf(in.boomLeanDeg) > LEAN_WARN_DEG) out.statusFlags |= STATUS_LEAN_WARN;
    if (in.estopLatched)    out.statusFlags |= STATUS_ESTOP;

    if (!loadValid) {
        out.loadPercent = LOAD_PERCENT_SENSOR_FAULT;
        out.alarmLevel  = 2;
    } else {
        float load = out.actualLoadKg > 0.0f ? out.actualLoadKg : 0.0f;
        float pct;
        if (out.safeLoadLimit > 0.0f) {
            pct = load / out.safeLoadLimit * 100.0f;
        } else {
            // Outside the chart (limit 0): any real load is an overload
            pct = (load > LOAD_ZERO_DEADBAND_KG) ? LOAD_PERCENT_CAP : 0.0f;
        }
        if (pct > LOAD_PERCENT_CAP) pct = LOAD_PERCENT_CAP;
        out.loadPercent = pct;

        // Escalate at once; only step down once clearly below the level's threshold
        const uint8_t target = (pct >= ALARM_CRITICAL_PERCENT) ? 2
                             : (pct >= ALARM_WARN_PERCENT)     ? 1 : 0;
        if (target > state.level) {
            state.level = target;
        } else if (target < state.level) {
            const float threshold = (state.level == 2) ? ALARM_CRITICAL_PERCENT : ALARM_WARN_PERCENT;
            if (pct < threshold - ALARM_HYSTERESIS_PERCENT) state.level = target;
        }
        out.alarmLevel = state.level;
    }

    // Without a trustworthy boom angle or extension the chart limit is meaningless
    if ((!in.boomSensorOK || !in.extSensorOK || in.boomMismatch) && out.alarmLevel < 2) out.alarmLevel = 2;

    // A latched E-stop outranks every load-based alarm until RST
    if (in.estopLatched) out.alarmLevel = 3;

    return out;
}
