// ============================================================
//  Advanced SLI — ESP32 Main Entry Point
//  FreeRTOS-based firmware for the Safe Load Indicator system.
//
//  This file:
//    1. Initializes all I2C buses and sensor drivers
//    2. Creates the shared sensor-data mutex
//    3. Spawns FreeRTOS tasks pinned to specific CPU cores
//
//  Core 1 (real-time critical):
//    - SensorTask  [100Hz, Priority 5] — reads all sensors
//
//  Core 0 (communication):
//    - TelemetryTask [50Hz, Priority 3] — pushes $T packets to PC
//    - CommandTask   [event, Priority 4] — parses G-code from PC
// ============================================================
#include <Wire.h>
#include "config.h"
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
#include "load_chart.h"
#include "load_comp.h"
#include "safety_task.h"

// ======================== GLOBAL RTOS OBJECTS ========================
SemaphoreHandle_t sensorMutex = NULL;
volatile bool     g_estopLatched = false;

// ======================== SHARED SENSOR DATA ========================
SensorData sensorData;

// ======================== I2C BUSES ========================
// Bus 2: Software I2C for telescope AS5600 ONLY (MPU6050 moved to Wire/Bus0)
SoftI2C softI2C(I2C2_SDA, I2C2_SCL);

// ======================== SENSOR DRIVERS ========================
AS5600Driver swingEncoder(AS5600Driver::BUS_WIRE0);            // Bus 0 (shares with MPU)
AS5600Driver boomEncoder(AS5600Driver::BUS_WIRE1);             // Bus 1
AS5600Driver teleEncoder(AS5600Driver::BUS_SOFT, &softI2C);    // Bus 2

MPU6050Driver imu;                                             // Wire Bus 0 (GPIO 21/22)

HX711Driver loadCell(HX711_DT_PIN, HX711_SCK_PIN);
FSRReader   fsrReader;
N20Encoder  winchEncoder;

// ======================== SAFETY DATA ========================
LoadChart        loadChart;   // rated-capacity table, persisted in NVS
LoadCompensation loadComp;    // boom-angle correction for the load cell, persisted in NVS

// ======================== COMMUNICATION ========================
MegaBridge megaBridge;

// ======================== SETUP ========================
void setup() {
    // --- USB Serial to PC ---
    Serial.begin(USB_BAUD);
    delay(500);
    Serial.println("\n=== Advanced SLI ESP32 Starting ===");

    // --- Initialize I2C Buses ---
    // 100kHz standard mode ensures reliable signal integrity with breadboards/crane wiring
    Serial.print("Init I2C Bus 0 (Wire 21/22)... ");
    Wire.begin(I2C0_SDA, I2C0_SCL);
    Wire.setClock(100000);
    Wire.setTimeOut(50);
    Serial.println("OK");

    Serial.print("Init I2C Bus 1 (Wire1 25/26)... ");
    Wire1.begin(I2C1_SDA, I2C1_SCL);
    Wire1.setClock(100000);
    Wire1.setTimeOut(50);
    Serial.println("OK");

    Serial.print("Init I2C Bus 2 (SoftI2C 32/33)... ");
    softI2C.begin();
    Serial.println("OK");

    // --- Initialize Sensors ---
    // Encoders initialized first to ensure clean I2C probe before high-rate IMU traffic
    Serial.print("AS5600 Swing (21/22): ");
    Serial.println(swingEncoder.begin() ? "DETECTED" : "NOT FOUND");

    Serial.print("AS5600 Boom (25/26):  ");
    Serial.println(boomEncoder.begin() ? "DETECTED" : "NOT FOUND");

    Serial.print("AS5600 Tele (32/33):  ");
    Serial.println(teleEncoder.begin() ? "DETECTED" : "NOT FOUND");

    // Initialize MPU6050 on Wire (Bus 0)
    Serial.print("MPU6050 IMU (21/22):  ");
    if (imu.begin()) {
        Wire.setClock(100000);
        Serial.println("DETECTED — Calibrating gyro (keep still)...");
        imu.calibrateGyro(200);  // Quick calibration on startup
        Wire.setClock(100000);
        Serial.println("  Gyro calibrated.");
    } else {
        Serial.println("NOT FOUND");
    }

    Serial.print("HX711 Load Cell: ");
    loadCell.begin();
    Serial.println(loadCell.isConnected() ? "DETECTED" : "NOT FOUND");

    Serial.print("FSR Readers:   ");
    fsrReader.begin();
    Serial.println("CONFIGURED (ADC1)");

    Serial.print("N20 Encoder:   ");
    winchEncoder.begin();
    Serial.println("CONFIGURED (Interrupts attached)");

    // --- Initialize Mega Bridge (UART2) ---
    Serial.print("Mega UART2:    ");
    megaBridge.begin(&Serial2);
    Serial.println("CONFIGURED");

    // --- Initialize shared sensor data to safe defaults ---
    memset(&sensorData, 0, sizeof(SensorData));
    sensorData.safeLoadLimit = SAFE_LOAD_DEFAULT_KG;  // until SafetyTask's first pass
    sensorData.sensorsOK = true;

    // --- Create RTOS synchronization primitives ---
    sensorMutex = xSemaphoreCreateMutex();

    if (!sensorMutex) {
        Serial.println("FATAL: Failed to create RTOS primitives!");
        while (1) delay(1000);  // Halt
    }

    // --- Initialize task subsystems ---
    sensorTask_init(
        &swingEncoder, &boomEncoder, &teleEncoder,
        &imu, &loadCell, &fsrReader, &winchEncoder
    );
    sensorTask_loadCalibration();  // Restore zero offsets and axis config from flash

    loadChart.loadFromFlash();     // Restore saved load chart (if any)
    Serial.print("Load chart:     ");
    Serial.print(loadChart.count());
    Serial.println(" entries in flash");

    loadComp.loadFromFlash();      // Restore saved load-cell angle correction (if any)
    Serial.print("Load cell gain: ");
    Serial.print(loadComp.count());
    Serial.println(" angle points in flash");

    safetyTask_init(&loadChart, &loadComp);

    commandTask_init(
        &megaBridge, &winchEncoder, &loadCell,
        &imu, &teleEncoder, &swingEncoder, &boomEncoder,
        &loadChart, &loadComp
    );

    // --- Create FreeRTOS Tasks ---
    // Core 1: Time-critical sensor acquisition
    xTaskCreatePinnedToCore(
        sensorTask,              // Task function
        "SensorTask",            // Name (for debug)
        SENSOR_TASK_STACK,       // Stack size (bytes)
        NULL,                    // Parameter
        SENSOR_TASK_PRIORITY,    // Priority (5 = high)
        NULL,                    // Task handle
        1                        // Pin to Core 1
    );

    // Core 1: Safety — load correction, limit lookup, alarm level (highest priority)
    xTaskCreatePinnedToCore(
        safetyTask,
        "SafetyTask",
        SAFETY_TASK_STACK,
        NULL,
        SAFETY_TASK_PRIORITY,    // Priority 6
        NULL,
        1                        // Pin to Core 1
    );

    // Core 0: Telemetry push to PC
    xTaskCreatePinnedToCore(
        telemetryTask,
        "TelemetryTask",
        TELEMETRY_TASK_STACK,
        NULL,
        TELEMETRY_TASK_PRIORITY,  // Priority 3
        NULL,
        0                         // Pin to Core 0
    );

    // Core 0: Command parser (PC → ESP32)
    xTaskCreatePinnedToCore(
        commandTask,
        "CommandTask",
        COMMAND_TASK_STACK,
        NULL,
        COMMAND_TASK_PRIORITY,    // Priority 4
        NULL,
        0                         // Pin to Core 0
    );

    Serial.println("=== All tasks started. System ready. ===\n");
}

// ======================== LOOP ========================
// With FreeRTOS, loop() is effectively idle.
// We keep it empty — all work is done in tasks.
void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));  // Yield to RTOS scheduler
}
