// ============================================================
//  Sensor Task — FreeRTOS Task (Core 1, Priority 5)
//  Reads ALL sensors at SENSOR_RATE_HZ and writes to the
//  shared SensorData struct (mutex-protected).
//
//  It is also the only task that touches the I2C buses and the HX711,
//  so every calibration that needs the hardware (tare, encoder zero,
//  IMU zero) is queued here instead of running on the command core.
// ============================================================
#ifndef SENSOR_TASK_H
#define SENSOR_TASK_H

#include "config.h"
#include "as5600_driver.h"
#include "mpu6050_driver.h"
#include "hx711_driver.h"
#include "fsr_reader.h"
#include "n20_encoder.h"

// Encoder axis indexes (G-code A1..A3 = index + 1, same numbering as M1..M3)
enum EncoderIndex : uint8_t { ENC_SWING = 0, ENC_BOOM = 1, ENC_TELE = 2, ENC_COUNT = 3 };
// Calibration axis id for the rope (N20 winch encoder): G-code A4
static const uint8_t CAL_AXIS_ROPE = 3;

/** A calibration step for SensorTask to run on core 1. */
struct CalRequest {
    CalCommand type;   // CAL_TARE_LOAD, CAL_RESET_TELE, CAL_ENC_REF, CAL_ENC_INVERT
    uint8_t    axis;   // EncoderIndex or CAL_AXIS_ROPE (CAL_ENC_*)
    float      value;  // CAL_ENC_REF: new value; CAL_RESET_TELE: scale mm/rev (0 = keep); CAL_ROPE_SCALE: mm/count
    int8_t     flag;   // CAL_ENC_INVERT / CAL_RESET_TELE: 0/1 invert, -1 = keep
};

/** Per-encoder diagnostics for the DBG report. */
struct EncoderDiag {
    bool               connected;
    AS5600MagnetStatus magnet;
    uint32_t           reads, fails;   // single I2C transfers
    uint32_t           rejected;       // samples dropped by the glitch gate
    uint32_t           resyncs;        // times motion had to be skipped (counts lost)
    bool               inverted;
    float              reference;
    float              scale;          // units per encoder revolution
};

struct SensorDiag {
    EncoderDiag enc[ENC_COUNT];
    float   teleScale;          // mm per encoder revolution
    int32_t ropeCounts;         // N20 counts since the rope reference (direction-corrected)
    float   ropeScale;          // mm per N20 count
    bool    ropeInverted;
    bool    boomCalibrated;     // boom encoder zeroed (CAL4 A2) -> boom angle comes from it
    bool    boomFromEncoder;    // boom angle source right now
    float   loopAvgMs;          // SensorTask work time per cycle
    float   loopMaxMs;
    float   periodMaxMs;        // longest gap between cycles (should stay ~10 ms)
    int32_t hx711Raw;
    float   loadScale;              // HX711 counts per kg
    uint8_t loadPoints;             // calibration weights recorded this session
    bool    loadScaleCalibrated;    // false = still the config.h default
    // Worst time per loop stage since the last DBG (ms): finds what stalls the loop
    float   stageMaxMs[7];          // see STAGE_* in sensor_task.cpp
    uint32_t imuReconnects;         // IMU fast re-inits after dropouts
    uint32_t imuRejected;           // implausible (corrupted) IMU samples skipped
};

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

/**
 * Hardware-I2C bus task (core 1): reads the swing (bus 3) and boom (bus 1)
 * AS5600s and the MPU6050 (bus 0), and runs IMU zeroing. Kept separate so an
 * I2C hang there (>= 1 s in this ESP32 core) can't freeze the telescope,
 * load cell, FSRs or winch in sensorTask.
 */
void sensorTask_hwBus(void* pvParameters);

/** Safely request IMU calibration to be processed on Core 1 (avoids I2C collisions).
 *  @param boomSrc   0=Roll(X), 1=Pitch(Y) — physical source axis for the IMU boom angle
 *  @param boomInv   true to negate boom output
 *  @param tiltSrc   0=Roll(X), 1=Pitch(Y) — physical source axis for tilt angle (independent)
 *  @param tiltInv   true to negate tilt output
 */
void sensorTask_requestIMUCalibration(uint8_t boomSrc, bool boomInv, uint8_t tiltSrc, bool tiltInv);

/** Queue a tare / encoder calibration for core 1. The $ACK is sent when it has run. */
bool sensorTask_requestCal(const CalRequest& req);

/** Load calibration offsets and axis settings from NVS flash */
void sensorTask_loadCalibration();

/** Copy the latest diagnostics. resetPeaks clears the max loop / period figures. */
void sensorTask_getDiag(SensorDiag* out, bool resetPeaks);

#endif // SENSOR_TASK_H
