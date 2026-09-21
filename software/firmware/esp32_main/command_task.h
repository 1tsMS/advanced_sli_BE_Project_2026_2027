// ============================================================
//  Command Task — FreeRTOS Task (Core 0, Priority 4)
//  Listens on USB Serial for incoming G-code commands from PC,
//  parses them, and dispatches to Mega / DRV8833 / calibration.
// ============================================================
#ifndef COMMAND_TASK_H
#define COMMAND_TASK_H

#include "config.h"
#include "gcode_parser.h"
#include "mega_bridge.h"
#include "n20_encoder.h"
#include "hx711_driver.h"
#include "mpu6050_driver.h"
#include "as5600_driver.h"
#include "telemetry_fmt.h"
#include "load_chart.h"
#include "load_comp.h"

/**
 * Initialize the command task with references to subsystems.
 * Call ONCE in setup() before creating the task.
 */
void commandTask_init(
    MegaBridge* mega,
    N20Encoder* winch,
    HX711Driver* loadCell,
    MPU6050Driver* imu,
    AS5600Driver* teleEnc,
    AS5600Driver* swingEnc,
    AS5600Driver* boomEnc,
    LoadChart* loadChart,
    LoadCompensation* loadComp
);

/**
 * FreeRTOS task — blocks on Serial.available(), parses G-code,
 * dispatches motor commands and calibration actions.
 */
void commandTask(void* pvParameters);

#endif // COMMAND_TASK_H
