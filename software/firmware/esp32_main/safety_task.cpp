// ============================================================
//  Safety Task — Implementation
// ============================================================
#include "safety_task.h"
#include "safety_eval.h"

static LoadChart*        _chart = nullptr;
static LoadCompensation* _comp  = nullptr;

// Global sensor data — declared in esp32_main.ino
extern SensorData sensorData;

void safetyTask_init(LoadChart* chart, LoadCompensation* comp) {
    _chart = chart;
    _comp  = comp;
}

void safetyTask(void* pvParameters) {
    SafetyState state;
    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(SAFETY_PERIOD_MS);

    for (;;) {
        if (_chart && _comp && xSemaphoreTake(sensorMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            SafetyInputs in;
            in.boomAngle    = sensorData.boomAngle;
            in.extensionMM  = sensorData.extensionMM;
            in.measuredKg   = sensorData.loadCellRaw;
            in.boomLeanDeg  = sensorData.boomLean;
            in.loadSensorOK = sensorData.loadSensorOK;
            in.boomSensorOK = sensorData.boomSensorOK;
            in.boomMismatch = sensorData.boomMismatch;
            in.extSensorOK  = sensorData.extSensorOK;
            in.estopLatched = g_estopLatched;

            // One short critical section: the chart and gain table are only
            // swapped under this same mutex, so they can be read here safely.
            SafetyOutputs out = safetyEvaluate(in, state, *_chart, *_comp);

            sensorData.actualLoadKg  = out.actualLoadKg;
            sensorData.safeLoadLimit = out.safeLoadLimit;
            sensorData.loadPercent   = out.loadPercent;
            sensorData.alarmLevel    = out.alarmLevel;
            sensorData.statusFlags   = out.statusFlags;

            xSemaphoreGive(sensorMutex);
        }

        vTaskDelayUntil(&lastWakeTime, period);
    }
}
