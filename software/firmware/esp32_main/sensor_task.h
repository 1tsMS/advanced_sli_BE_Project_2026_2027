// ============================================================
//  Sensor Task — FreeRTOS Task (Core 1, Priority 5)
//  Reads ALL sensors at SENSOR_RATE_HZ and writes to the
//  shared SensorData struct (mutex-protected).
// ============================================================
#ifndef SENSOR_TASK_H
#define SENSOR_TASK_H

#include "config.h"
#include "as5600_driver.h"
#include "mpu6050_driver.h"
#include "hx711_driver.h"
#include "fsr_reader.h"
#include "n20_encoder.h"

/**
 * Initialize all sensor driver instances.
 * Call ONCE in setup() before creating the task.
 */
void sensorTask_init(
    AS5600Driver* swingEnc,
    AS5600Driver* boomEnc,
    AS5600Driver* teleEnc,
    MPU6050Driver* imu,
    HX711Driver* loadCell,
    FSRReader* fsr,
    N20Encoder* winchEnc
);

/**
 * FreeRTOS task function — runs forever at SENSOR_RATE_HZ.
 * Reads all sensors → writes to global SensorData → releases mutex.
 */
void sensorTask(void* pvParameters);

/** Zero both boom and tilt angles to current position and save to flash */
void sensorTask_zeroIMU();

/** Safely request IMU calibration to be processed on Core 1 (avoids I2C collisions).
 *  @param boomSrc   0=Roll(X), 1=Pitch(Y) — physical source axis for boom angle
 *  @param boomInv   true to negate boom output
 *  @param tiltSrc   0=Roll(X), 1=Pitch(Y) — physical source axis for tilt angle (independent)
 *  @param tiltInv   true to negate tilt output
 */
void sensorTask_requestIMUCalibration(uint8_t boomSrc, bool boomInv, uint8_t tiltSrc, bool tiltInv);

/**
 * State of the IMU-vs-encoder boom angle cross-check, for the DBG report.
 * @param errDeg  |encoder-derived angle - IMU angle| from the last comparison
 * @return 0=OFF (encoder or IMU unavailable), 1=LEARNING (direction not learned yet),
 *         2=OK, 3=MISMATCH
 */
uint8_t sensorTask_getBoomCheck(float* errDeg);

/** Load calibration offsets and axis settings from NVS flash */
void sensorTask_loadCalibration();

/** Reset telescope encoder zero, optionally updating scale and invert direction.
 *  @param scale   Custom mm per revolution (0 = keep current)
 *  @param invert  Inversion flag: 0=normal, 1=inverted, -1=keep current
 */
void sensorTask_resetTelescope(float scale = 0.0f, int8_t invert = -1);

/** Current telescope calibration (loaded from flash) — reported to the dashboard via DBG */
float sensorTask_getTeleScale();
bool  sensorTask_getTeleInvert();

#endif // SENSOR_TASK_H

