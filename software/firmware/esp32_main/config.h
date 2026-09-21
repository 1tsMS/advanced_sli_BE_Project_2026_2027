// ============================================================
//  Advanced SLI — ESP32 Master Configuration
//  All pin assignments, I2C addresses, timing constants,
//  and shared data structures for the entire firmware.
// ============================================================
#ifndef SLI_CONFIG_H
#define SLI_CONFIG_H

#include <Arduino.h>

// ======================== I2C BUS PINS ========================
// Bus 0: Hardware Wire — MPU6050 + AS5600 Swing Encoder
// MPU6050 confirmed working on GPIO21/22 from mpu_hx711_test.ino
#define I2C0_SDA  21
#define I2C0_SCL  22

// Bus 1: Hardware Wire1 — AS5600 Boom Lift Encoder
#define I2C1_SDA  25
#define I2C1_SCL  26

// Bus 2: Software bit-bang — AS5600 Telescope only
// MPU6050 moved to Bus 0 (confirmed working there)
#define I2C2_SDA  32
#define I2C2_SCL  33

// ======================== I2C ADDRESSES ========================
#define AS5600_ADDR   0x36
#define MPU6050_ADDR  0x68

// ======================== HX711 LOAD CELL ========================
// Pins confirmed from mpu_hx711_test.ino
#define HX711_DT_PIN         17
#define HX711_SCK_PIN        16
// Default calibration factor (counts per kg) from software/test_codes/loadcell_approx/loadcell_approx.ino
#define HX711_DEFAULT_SCALE  122000.0f
// Plausible load ceiling for a 5kg model crane (beyond this = uncalibrated or overload error)
#define MAX_VALID_LOAD_KG    10.0f

// ======================== FSR ANALOG PINS ========================
// ADC1 input-only pins — outrigger force sensors
#define FSR1_PIN  34
#define FSR2_PIN  35
#define FSR3_PIN  36
#define FSR4_PIN  39

// ======================== N20 WINCH VIA DRV8833 ========================
#define DRV_AIN1  13   // PWM / direction
#define DRV_AIN2  14   // PWM / direction
#define DRV_STBY  12   // Standby (tie to 3.3V if always-on)

// ======================== N20 QUADRATURE ENCODER ========================
#define N20_ENC_A  18  // Interrupt-capable
#define N20_ENC_B  19  // Interrupt-capable

// ======================== UART2 TO ARDUINO MEGA ========================
// Confirmed from test_codes/esp_serial_test/esp_serial_test.ino
#define MEGA_RX_PIN  23   // ESP32 GPIO23 (RX2) ← Mega Pin 18 (TX1)
#define MEGA_TX_PIN  27   // ESP32 GPIO27 (TX2) → Mega Pin 19 (RX1)
#define MEGA_BAUD    9600

// ======================== USB SERIAL TO PC ========================
#define USB_BAUD  115200

// ======================== FREERTOS TASK CONFIG ========================
// Sensor acquisition rate
#define SENSOR_RATE_HZ       100
#define SENSOR_PERIOD_MS     (1000 / SENSOR_RATE_HZ)

// Telemetry push rate to PC
#define TELEMETRY_RATE_HZ    50
#define TELEMETRY_PERIOD_MS  (1000 / TELEMETRY_RATE_HZ)

// Stack sizes (bytes) — increase if you see stack overflow crashes
#define SENSOR_TASK_STACK     4096
#define TELEMETRY_TASK_STACK  4096
#define COMMAND_TASK_STACK    8192   // LC GET / DBG / CAL3 buffers on the stack + float printf

// Task priorities — higher number = higher priority
#define SENSOR_TASK_PRIORITY     5   // Time-critical sensor reads
#define COMMAND_TASK_PRIORITY    4   // Must respond to E-stop quickly
#define TELEMETRY_TASK_PRIORITY  3   // Can tolerate slight delays

// Safety task: load calculation + alarm levels (runs on core 1)
#define SAFETY_RATE_HZ        50
#define SAFETY_PERIOD_MS      (1000 / SAFETY_RATE_HZ)
#define SAFETY_TASK_STACK     4096
#define SAFETY_TASK_PRIORITY  6   // Highest: a safety check must never wait behind sensor reads

// ======================== ENCODER CONVERSIONS ========================
// N20 encoder: ticks per full drum revolution
// 150RPM N20 with magnetic encoder typically has ~7 PPR × 4 (quadrature) × gear ratio
// Adjust after measuring your actual encoder!
#define N20_TICKS_PER_REV       840
// Rope drum diameter in mm
#define ROPE_DRUM_DIAMETER_MM   20.0f
// Derived: mm of rope per encoder tick
#define ROPE_MM_PER_TICK  ((PI * ROPE_DRUM_DIAMETER_MM) / N20_TICKS_PER_REV)

// Telescope AS5600: mm of linear travel per full encoder revolution
// Calibrated from physical test: 50mm actual = 2.625 revs (-21mm at 8mm/rev) -> 19.048 mm/rev
#define TELE_MM_PER_REVOLUTION  19.048f
#define TELE_DEFAULT_INVERT     true


// ======================== LOAD / ALARM SETTINGS ========================
// Safe working load used ONLY while no load chart has been uploaded
#define SAFE_LOAD_DEFAULT_KG        5.0f
// Alarm thresholds as % of the safe load (WARN at 80, CRITICAL at 100)
#define ALARM_WARN_PERCENT          80.0f
#define ALARM_CRITICAL_PERCENT      100.0f
// A level is only left once load falls this far below its threshold (stops flapping)
#define ALARM_HYSTERESIS_PERCENT    2.0f
// Reported load% is capped here; loadPercent = 999 is reserved for "load sensor fault"
#define LOAD_PERCENT_CAP            500.0f
#define LOAD_PERCENT_SENSOR_FAULT   999.0f
// Below this the load counts as zero when the safe limit is 0 kg (outside the chart)
#define LOAD_ZERO_DEADBAND_KG       0.05f
// The load cell is considered dead if no new HX711 sample arrives for this long
#define LOAD_SENSOR_STALE_MS        1000

// Boom lean beyond this (either direction) sets STATUS_LEAN_WARN. Display only.
#define LEAN_WARN_DEG               5.0f

// ---- Status flags (SensorData::statusFlags, sent as field 12 of the $T packet) ----
// The dashboard shows these as-is; it does not re-derive any of them.
#define STATUS_LOAD_FAULT    (1u << 0)  // load cell stale, or reading outside 0..MAX_VALID_LOAD_KG
#define STATUS_BOOM_FAULT    (1u << 1)  // boom-angle IMU not responding: boom angle unreliable
#define STATUS_EXT_FAULT     (1u << 2)  // telescope encoder not responding: extension unreliable
#define STATUS_NO_CHART      (1u << 3)  // no load chart saved, default limit in use
#define STATUS_OUT_OF_CHART  (1u << 4)  // position outside the load chart, limit is 0 kg
#define STATUS_LEAN_WARN     (1u << 5)  // boom leaning sideways beyond LEAN_WARN_DEG
#define STATUS_ESTOP         (1u << 6)  // E-stop latched, waiting for RST
#define STATUS_BOOM_MISMATCH (1u << 7)  // IMU and boom encoder disagree: boom angle cannot be trusted

// ======================== SHARED DATA STRUCTURES ========================

/**
 * Central sensor data struct — the single source of truth.
 * Written by SensorTask, read by TelemetryTask (and SafetyTask in Phase 3).
 * All access MUST be guarded by sensorMutex.
 */
struct SensorData {
    // Angle values (degrees, 0.0–360.0)
    float boomAngle;
    float swingAngle;

    // Computed linear values
    float extensionMM;       // telescope linear extension
    float ropeLengthMM;      // rope payed out from N20 encoder

    // Load
    float loadCellRaw;       // kg — direct HX711 reading, NOT angle-compensated
    float actualLoadKg;      // kg — angle-compensated (written by SafetyTask)

    // Boom lean: sideways tilt of the boom (rotation about its own axis), from the
    // boom-mounted IMU. NOT a chassis tilt: it also moves with swing and boom twist.
    float boomLean;          // degrees

    // Outrigger force sensors (raw ADC 0–4095)
    uint16_t fsr[4];

    // Safety status (all written by SafetyTask)
    float safeLoadLimit;     // kg — from the load chart (SAFE_LOAD_DEFAULT_KG if no chart)
    float loadPercent;       // (actualLoad / safeLimit) × 100; 999 = load sensor fault
    uint8_t alarmLevel;      // 0=OK, 1=WARN, 2=CRITICAL, 3=ESTOP

    // System health
    bool sensorsOK;          // true if all expected I2C devices respond
    bool loadSensorOK;       // HX711 is delivering fresh samples (written by SensorTask)
    bool boomSensorOK;       // boom-angle IMU responding (written by SensorTask)
    bool boomMismatch;       // IMU and boom encoder disagree (written by SensorTask)
    bool extSensorOK;        // telescope encoder responding (written by SensorTask)
    uint16_t statusFlags;    // STATUS_* bits, written by SafetyTask, sent in the $T packet
};

// ======================== MOTION SAFETY ========================
// Highest speed value accepted in an M-command (steps/s for steppers).
// The winch scales this range onto its 0-255 PWM.
#define MOTOR_SPEED_MAX      2500
// Dead-man timeout: a motor keeps running only while its M-command keeps being
// repeated. If nothing arrives for this long (link lost, browser frozen, button
// release missed) the axis is stopped. Same value is used in mega_executor.ino.
#define MOTION_TIMEOUT_MS    400

/**
 * Motor command — parsed from G-code, forwarded to Mega or DRV8833.
 */
struct MotorCommand {
    uint8_t  axis;       // 1=Swing, 2=Lift, 3=Tele, 4=Winch, 0=StopAxis
    uint16_t speed;      // 0–MOTOR_SPEED_MAX (steps/s)
    uint8_t  direction;  // 0 or 1
    uint8_t  stopAxis;   // which axis to stop (for M0 Ax commands)
    bool     isEstop;    // true → emergency stop ALL motors
};

/**
 * Calibration command types
 */
enum CalCommand : uint8_t {
    CAL_TARE_LOAD   = 0,  // Zero the load cell
    CAL_ZERO_IMU    = 1,  // Set current IMU orientation as zero
    CAL_RESET_TELE  = 2,  // Reset telescope extension to 0
    CAL_LOAD_GAIN   = 3   // Record / clear the boom-angle load correction
};

// ======================== GLOBAL RTOS HANDLES ========================
// Declared here, defined in esp32_main.ino
extern SemaphoreHandle_t sensorMutex;

// E-stop latch: set by T0, blocks all motion commands until an explicit RST.
// Kept separate from SensorData.alarmLevel because SensorTask rewrites that every cycle.
extern volatile bool g_estopLatched;

#endif // SLI_CONFIG_H
