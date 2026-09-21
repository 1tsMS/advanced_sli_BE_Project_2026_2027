// ============================================================
//  Safety Task — FreeRTOS Task (Core 1, Priority 6 — highest)
//  Every SAFETY_PERIOD_MS: takes the latest sensor values, applies the boom
//  angle correction, looks up the safe load in the load chart and writes the
//  resulting load %, limit and alarm level back to SensorData.
//  ALARM ONLY: this task never stops or blocks a motor.
// ============================================================
#ifndef SAFETY_TASK_H
#define SAFETY_TASK_H

#include "config.h"
#include "load_chart.h"
#include "load_comp.h"

/** Call ONCE in setup() before creating the task. */
void safetyTask_init(LoadChart* chart, LoadCompensation* comp);

void safetyTask(void* pvParameters);

#endif // SAFETY_TASK_H
