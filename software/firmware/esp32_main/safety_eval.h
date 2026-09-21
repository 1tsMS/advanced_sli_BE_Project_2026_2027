// ============================================================
//  Safety Evaluation — pure load / limit / alarm calculation
//
//  No RTOS, no hardware: everything SafetyTask decides lives here so it
//  can be unit-tested on a PC. SafetyTask just feeds it the latest sensor
//  values (under sensorMutex) and stores the result.
// ============================================================
#ifndef SAFETY_EVAL_H
#define SAFETY_EVAL_H

#include "config.h"
#include "load_chart.h"
#include "load_comp.h"

struct SafetyInputs {
    float boomAngle;       // deg
    float extensionMM;     // mm
    float measuredKg;      // uncorrected HX711 reading
    float boomLeanDeg;     // sideways lean of the boom
    bool  loadSensorOK;    // HX711 delivering fresh samples
    bool  boomSensorOK;    // boom-angle IMU responding
    bool  boomMismatch;    // IMU and boom encoder disagree
    bool  extSensorOK;     // telescope encoder responding
    bool  estopLatched;
};

struct SafetyOutputs {
    float    actualLoadKg;   // angle-compensated load
    float    safeLoadLimit;  // kg from the chart (SAFE_LOAD_DEFAULT_KG if none)
    float    loadPercent;    // 0..LOAD_PERCENT_CAP, or LOAD_PERCENT_SENSOR_FAULT
    uint8_t  alarmLevel;     // 0=OK 1=WARN 2=CRITICAL 3=ESTOP
    uint16_t statusFlags;    // STATUS_* bits (see config.h)
};

/** Memory kept between evaluations (alarm hysteresis). */
struct SafetyState {
    uint8_t level = 0;      // load-based level 0..2
};

/**
 * Compute load, limit, percent and alarm level.
 * The caller must hold sensorMutex: this reads the chart and gain table.
 *
 * A faulty load sensor (stale, or reading outside 0..MAX_VALID_LOAD_KG) raises
 * a CRITICAL alarm with loadPercent = LOAD_PERCENT_SENSOR_FAULT. A dead boom-angle
 * or telescope sensor also raises CRITICAL, because the chart lookup would use a
 * wrong position; so does an IMU/encoder disagreement. It only alarms; nothing here
 * ever blocks motion.
 */
SafetyOutputs safetyEvaluate(const SafetyInputs& in, SafetyState& state,
                             const LoadChart& chart, const LoadCompensation& comp);

#endif // SAFETY_EVAL_H
