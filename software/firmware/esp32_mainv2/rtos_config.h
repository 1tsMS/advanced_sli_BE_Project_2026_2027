// ============================================================
//  RTOS layout: every task, its core, priority, stack and rate in ONE place
//
//  CORE 1 is reserved for the hard 100 Hz measurement loop:
//
//    SensorTask    prio 5  100 Hz  telescope AS5600 (soft I2C), HX711, FSR ADC,
//                                  N20 PCNT count, ALL encoder maths (single owner),
//                                  calibration requests, shared SensorData
//    SafetyTask    prio 4  50 Hz   the SLI: load % vs the load chart, blocked-motion
//                                  mask, E-stop latch, bypass timer, $S status.
//                                  Below SensorTask so a measurement is never late;
//                                  on core 1 so the busy-waiting I2C on core 0
//                                  can't delay a cut.
//
//  CORE 0 does communication and everything that may wait:
//
//    CommandTask   prio 5  event   wakes on USB RX (onReceive notification): G-code,
//                                  E-stop, Mega bridge, winch dead-man (checked every
//                                  20 ms even with no input). Highest on core 0 so an
//                                  E-stop is handled first.
//    SerialTxTask  prio 4  50 Hz   the ONLY writer of USB serial: queued ACK/DBG lines
//                                  as they arrive, $T telemetry on schedule
//    HwBusTask     prio 3  100 Hz  swing + boom AS5600 and MPU6050 (soft I2C), IMU
//                                  filter, non-blocking IMU calibration. Busy-waits on
//                                  bit-banged I2C, so it sits below comms on core 0.
//    StorageTask   prio 1  event   the ONLY writer of flash (NVS). Flash writes stall
//                                  the CPUs, so they never happen in the loops above.
//
//  Rules every task follows:
//    - Periodic tasks use vTaskDelayUntil (fixed rate, no drift); event tasks
//      block on a notification or queue. No delay()/busy loops waiting for data.
//    - One owner per resource: each I2C bus, the HX711, USB serial TX, Serial2 to
//      the Mega, NVS and the encoder maths each belong to exactly one task.
//      Others hand work over through a queue / notification / staged copy.
//    - Shared data: SensorData under sensorMutex (priority inheritance, short
//      holds, timeouts); small hand-over structs under spinlocks.
//    - Watchdog: real-time tasks feed the task watchdog every cycle; if one stops
//      for TASK_WDT_TIMEOUT_S the ESP32 resets (the Mega's dead-man then stops
//      the motors within 400 ms).
// ============================================================
#ifndef RTOS_CONFIG_H
#define RTOS_CONFIG_H

#include <Arduino.h>

// ---- Cores ----
#define CORE_REALTIME   1
#define CORE_COMMS      0

// ---- Priorities (higher number wins on its core) ----
#define PRIO_SENSOR     5
#define PRIO_SAFETY     4
#define PRIO_COMMAND    5
#define PRIO_SERIAL_TX  4
#define PRIO_HWBUS      3
#define PRIO_STORAGE    1

// ---- Stack sizes in bytes (ESP-IDF counts bytes). Headroom is shown in DBG ($D,STACKS) ----
#define STACK_SENSOR    6144
#define STACK_SAFETY    4096    // one LoadChart copy (~330 B) + $S formatting
#define STACK_COMMAND   8192    // DBG report buffer (1.5 KB) + parser
#define STACK_SERIAL_TX 4096
#define STACK_HWBUS     4096
#define STACK_STORAGE   4096    // Preferences / NVS

// ---- Rates ----
#define SENSOR_PERIOD_MS        10    // 100 Hz
#define HWBUS_PERIOD_MS         10    // 100 Hz
#define SAFETY_PERIOD_MS        20    // 50 Hz
#define SAFETY_STATUS_EVERY     10    // $S every 10th cycle (5 Hz) + on every change
#define TELEMETRY_PERIOD_MS_DEF 20    // 50 Hz (CFG RATE can change it)
#define COMMAND_IDLE_WAKE_MS    20    // dead-man check interval when no input
#define STORAGE_COALESCE_MS     200   // merge bursts of setting changes into one write

// ---- Task watchdog ----
#define TASK_WDT_TIMEOUT_S      3

// ---- Task handles (set in setup, used for notifications and DBG) ----
extern TaskHandle_t g_sensorTask;
extern TaskHandle_t g_safetyTask;
extern TaskHandle_t g_hwBusTask;
extern TaskHandle_t g_commandTask;
extern TaskHandle_t g_serialTxTask;
extern TaskHandle_t g_storageTask;

#endif // RTOS_CONFIG_H
