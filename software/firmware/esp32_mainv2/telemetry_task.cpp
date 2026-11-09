// ============================================================
//  SerialTxTask — Implementation
// ============================================================
#include "telemetry_task.h"
#include "telemetry_fmt.h"
#include "serial_out.h"
#include "esp_task_wdt.h"

// Global sensor data — declared in esp32_mainv2.ino
extern SensorData sensorData;

// Runtime-adjustable telemetry period
static volatile uint16_t _telemetryPeriodMs = TELEMETRY_PERIOD_MS_DEF;

void telemetryTask_setRate(uint16_t hz) {
    if (hz > 0 && hz <= 200) _telemetryPeriodMs = 1000 / hz;
}

void telemetryTask(void* pvParameters) {
    TelemetryFormatter formatter;
    char txBuffer[256];
    char line[SERIAL_LINE_MAX];
    static SensorData snapshot = {};   // if the mutex is busy, resend the previous sample

    esp_task_wdt_add(NULL);
    TickType_t nextDue = xTaskGetTickCount() + pdMS_TO_TICKS(_telemetryPeriodMs);

    for (;;) {
        esp_task_wdt_reset();

        // Before the deadline: sleep on the queue and write lines as they arrive.
        // (Signed difference so tick-counter wrap-around is handled.)
        const int32_t untilDue = (int32_t)(nextDue - xTaskGetTickCount());
        if (untilDue > 0) {
            if (serialOut_receive(line, (TickType_t)untilDue)) Serial.print(line);
            continue;
        }

        // Deadline reached: telemetry goes first, so a busy queue can't starve it
        if (xSemaphoreTake(sensorMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            snapshot = sensorData;
            sensorData.accelDynPk = 0.0f;   // peaks restart after every $T
            sensorData.jerkPk     = 0.0f;
            xSemaphoreGive(sensorMutex);
        }
        formatter.format(snapshot, txBuffer, sizeof(txBuffer));
        Serial.print(txBuffer);

        // Fixed schedule; if more than a period behind, restart from now instead of bursting
        nextDue += pdMS_TO_TICKS(_telemetryPeriodMs);
        if ((int32_t)(xTaskGetTickCount() - nextDue) >= 0) {
            nextDue = xTaskGetTickCount() + pdMS_TO_TICKS(_telemetryPeriodMs);
        }
    }
}
