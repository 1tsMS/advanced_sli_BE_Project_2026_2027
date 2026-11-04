// ============================================================
//  Advanced SLI — ESP32 firmware v2 (FreeRTOS)
//
//  setup() initialises the hardware, restores calibration from flash, then
//  starts the tasks below and deletes itself (the Arduino loop task).
//  The full task table, with the reasoning, is in rtos_config.h:
//
//    Core 1  SensorTask    prio 5  100 Hz  telescope, HX711, FSR, N20, encoder maths
//    Core 1  SafetyTask    prio 4  50 Hz   SLI: load chart, motion cut, E-stop latch
//    Core 0  CommandTask   prio 5  event   USB RX -> G-code, E-stop, Mega, winch dead-man
//    Core 0  SerialTxTask  prio 4  50 Hz   only writer of USB serial ($T + queued lines)
//    Core 0  HwBusTask     prio 3  100 Hz  swing/boom encoders + IMU (software I2C)
//    Core 0  StorageTask   prio 1  event   only writer of flash (NVS)
// ============================================================
#include "config.h"
#include "rtos_config.h"
#include "serial_out.h"
#include "persist.h"
#include "soft_i2c.h"
#include "as5600_driver.h"
#include "mpu6050_driver.h"
#include "hx711_driver.h"
#include "fsr_reader.h"
#include "n20_encoder.h"
#include "mega_bridge.h"
#include "sensor_task.h"
#include "telemetry_task.h"
#include "command_task.h"
#include "safety_task.h"
#include "esp_task_wdt.h"
#include "esp_system.h"

// ======================== GLOBAL RTOS OBJECTS ========================
SemaphoreHandle_t sensorMutex = NULL;

TaskHandle_t g_sensorTask   = NULL;
TaskHandle_t g_safetyTask   = NULL;
TaskHandle_t g_hwBusTask    = NULL;
TaskHandle_t g_commandTask  = NULL;
TaskHandle_t g_serialTxTask = NULL;
TaskHandle_t g_storageTask  = NULL;

const char* g_resetReason = "?";

// ======================== SHARED SENSOR DATA ========================
SensorData sensorData;

// ======================== I2C BUSES ========================
// All four buses are bit-banged (see soft_i2c.h): the hardware Wire driver in
// this core blocks for >= 1 s when noise hangs a transfer; this can't hang.
// Half-bit delay: 5 us, 8 us on bus 1 (kept from before its pull-ups were known).
// Each bus is used by exactly one task (bus 0, 1, 3: HwBusTask, bus 2: SensorTask).
SoftI2C i2cBus0(I2C0_SDA, I2C0_SCL, 5);   // MPU6050 only
SoftI2C i2cBus1(I2C1_SDA, I2C1_SCL, 8);   // boom AS5600
SoftI2C i2cBus2(I2C2_SDA, I2C2_SCL, 5);   // telescope AS5600
SoftI2C i2cBus3(I2C3_SDA, I2C3_SCL, 5);   // swing AS5600

// ======================== SENSOR DRIVERS ========================
AS5600Driver swingEncoder(&i2cBus3);
AS5600Driver boomEncoder(&i2cBus1);
AS5600Driver teleEncoder(&i2cBus2);
MPU6050Driver imu(&i2cBus0);

HX711Driver loadCell(HX711_DT_PIN, HX711_SCK_PIN);
FSRReader   fsrReader;
N20Encoder  winchEncoder;

// ======================== COMMUNICATION ========================
MegaBridge megaBridge;

static const char* resetReasonName(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:   return "POWER_ON";
        case ESP_RST_SW:        return "SOFTWARE";
        case ESP_RST_PANIC:     return "PANIC";
        case ESP_RST_INT_WDT:   return "INT_WATCHDOG";
        case ESP_RST_TASK_WDT:  return "TASK_WATCHDOG";
        case ESP_RST_WDT:       return "OTHER_WATCHDOG";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_EXT:       return "EXTERNAL";
        default:                return "UNKNOWN";
    }
}

/** Create a pinned task or halt with a message (a missing task is not a state to run in). */
static void startTask(TaskFunction_t fn, const char* name, uint32_t stack, UBaseType_t prio,
                      TaskHandle_t* handle, BaseType_t core) {
    if (xTaskCreatePinnedToCore(fn, name, stack, NULL, prio, handle, core) != pdPASS) {
        Serial.printf("FATAL: could not create %s\n", name);
        for (;;) delay(1000);
    }
}

// ======================== SETUP ========================
void setup() {
    // --- USB serial: buffers sized so writes rarely block, set before begin() ---
    Serial.setRxBufferSize(512);
    Serial.setTxBufferSize(1024);
    Serial.begin(USB_BAUD);
    delay(300);

    g_resetReason = resetReasonName(esp_reset_reason());
    Serial.printf("\n=== Advanced SLI ESP32 v2 starting (reset: %s) ===\n", g_resetReason);

    // --- Task watchdog: real-time tasks subscribe and feed it every cycle ---
    esp_task_wdt_init(TASK_WDT_TIMEOUT_S, true);   // true = reset the chip on timeout

    // --- RTOS objects that must exist before any task or calibration load ---
    sensorMutex = xSemaphoreCreateMutex();
    serialOut_init();
    if (!sensorMutex) {
        Serial.println("FATAL: could not create sensorMutex");
        for (;;) delay(1000);
    }

    // --- Sensors (setup runs before the tasks, so blocking init is fine here) ---
    i2cBus0.begin();
    i2cBus1.begin();
    i2cBus2.begin();
    i2cBus3.begin();
    Serial.printf("AS5600 swing (32/33): %s\n", swingEncoder.begin() ? "DETECTED" : "NOT FOUND");
    Serial.printf("AS5600 boom  (25/26): %s\n", boomEncoder.begin()  ? "DETECTED" : "NOT FOUND");
    Serial.printf("AS5600 tele  (15/5) : %s\n", teleEncoder.begin()  ? "DETECTED" : "NOT FOUND");

    if (imu.begin()) {
        Serial.println("MPU6050 (21/22): DETECTED, calibrating gyro (keep still)...");
        imu.calibrateGyro(200);
    } else {
        Serial.println("MPU6050 (21/22): NOT FOUND");
    }

    loadCell.begin();
    Serial.printf("HX711: %s\n", loadCell.isConnected() ? "DETECTED" : "NOT FOUND");
    fsrReader.begin();
    winchEncoder.begin();                      // PCNT + DRV8833

    Serial2.setTxBufferSize(256);              // M-commands to the Mega never block CommandTask
    megaBridge.begin(&Serial2);

    // --- Shared data defaults ---
    memset(&sensorData, 0, sizeof(SensorData));
    sensorData.safeLoadLimit = 5.0f;
    sensorData.sensorsOK = true;

    // --- Wire the tasks to their drivers and restore calibration from flash ---
    sensorTask_init(&swingEncoder, &boomEncoder, &teleEncoder,
                    &imu, &loadCell, &fsrReader, &winchEncoder);
    sensorTask_loadCalibration();
    safety_init();
    commandTask_init(&megaBridge, &winchEncoder, &loadCell,
                     &imu, &teleEncoder, &swingEncoder, &boomEncoder);

    // --- Start the tasks (table and reasoning in rtos_config.h) ---
    // Writers first, so nothing is lost when the others start reporting.
    startTask(storageTask,      "StorageTask",  STACK_STORAGE,   PRIO_STORAGE,   &g_storageTask,  CORE_COMMS);
    startTask(telemetryTask,    "SerialTxTask", STACK_SERIAL_TX, PRIO_SERIAL_TX, &g_serialTxTask, CORE_COMMS);
    startTask(commandTask,      "CommandTask",  STACK_COMMAND,   PRIO_COMMAND,   &g_commandTask,  CORE_COMMS);
    startTask(sensorTask_hwBus, "HwBusTask",    STACK_HWBUS,     PRIO_HWBUS,     &g_hwBusTask,    CORE_COMMS);
    startTask(sensorTask,       "SensorTask",   STACK_SENSOR,    PRIO_SENSOR,    &g_sensorTask,   CORE_REALTIME);
    startTask(safetyTask,       "SafetyTask",   STACK_SAFETY,    PRIO_SAFETY,    &g_safetyTask,   CORE_REALTIME);

    // USB input wakes CommandTask directly (no polling)
    Serial.onReceive(commandTask_onSerialRx);

    serialLine("=== All tasks started. System ready. ===");
}

// ======================== LOOP ========================
// Everything runs in tasks; the Arduino loop task has nothing to do, so it
// deletes itself instead of waking every second for nothing.
void loop() {
    vTaskDelete(NULL);
}
