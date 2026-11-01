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
// Motor wired so that AIN1-PWM pays out instead of winding up: swap in software so
// D0 stays "wind up" everywhere (dashboard, gamepad and the SLI's hoist cut rely on it).
// Set to 0 if the motor wires are ever swapped instead.
#define WINCH_REVERSED  1
// Winch runs at a fixed duty, whatever speed the command carries (255 = 100 %).
#define WINCH_PWM       230   // ~90 %
// DRV8833 EEP (nSLEEP): GPIO4 with a 10k pull-down to GND. The ESP32 itself drives
// GPIO14 at boot (a PWM burst) and leaves pins floating while flashing; with EEP tied
// to 3.3 V that spun the winch and the rope encoder missed the turns. The pull-down
// keeps the driver asleep (outputs off) until the firmware wakes it in N20Encoder::begin().
// GPIO4 has no boot function; GPIO12 is a strapping pin and must stay low at reset.
#define DRV_SLEEP  4

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
// Cores, priorities, stacks and rates of every task: see rtos_config.h
#include "rtos_config.h"

// Default telemetry rate (CFG RATE <hz> changes it at runtime)
#define TELEMETRY_RATE_HZ    50

// ======================== ENCODER CONVERSIONS ========================
// N20 winch encoder, counted by PCNT in 4x quadrature.
// Starting guess only (840 lines x 4, 20 mm drum). The real value is measured
// with the rope ruler calibration (CAL6) and stored in flash.
#define N20_COUNTS_PER_REV      3360

// Rope geometry around the calibrated length. The rope calibration (CAL4 A4 / CAL6)
// measures from the END OF THE LOAD CELL to the KNOT above the hook. Fixed parts
// the encoder never sees (measured 2026-10-29):
#define ROPE_FIXED_PATH_MM      245.0f   // 22.5 cm + 2 cm of rope before the load-cell end
#define ROPE_KNOT_TO_HOOK_MM     90.0f   // knot to hook tip
// -> ropeTotalMM = calibrated + 245 + 90      (all rope + hook)
// -> hookDropMM  = calibrated + 90            (load-cell end to hook tip: the part that
//                                              hangs and swings, i.e. the pendulum length)
#define ROPE_DRUM_DIAMETER_MM   20.0f
#define ROPE_MM_PER_COUNT_DEFAULT  ((PI * ROPE_DRUM_DIAMETER_MM) / N20_COUNTS_PER_REV)

// Telescope AS5600: mm of linear travel per full encoder revolution
// Calibrated from physical test: 50mm actual = 2.625 revs (-21mm at 8mm/rev) -> 19.048 mm/rev
#define TELE_MM_PER_REVOLUTION  19.048f
#define TELE_DEFAULT_INVERT     true

// Swing and boom AS5600s: degrees of axis rotation per encoder revolution.
// Magnet is on the motor shaft, NOT the output shaft, so these are geared.
// Swing: 20:1 cycloidal drive → 1 encoder rev = 360/20 = 18° of turntable.
// Boom:  leadscrew — ratio unknown, 360° placeholder until measured via CAL4.
#define SWING_DEG_PER_REV       18.0f
#define BOOM_DEG_PER_REV        360.0f

// Boom angle source. The boom AS5600 is on the leadscrew motor, and a leadscrew
// lifting a pivoting boom is NOT linear (deg per motor turn changes with angle),
// so a single BOOM_DEG_PER_REV can't give a correct angle. Keep 0 (IMU is the
// boom angle) until a multi-point angle-vs-turns calibration exists.
// 1 = use the boom encoder once it has been referenced with CAL4 A2.
#define BOOM_ANGLE_FROM_ENCODER 0

// Crane physical geometry
#define BASE_BOOM_LENGTH_MM     225.0f   // 22.5 cm physical base boom


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
    float teleAngle;         // raw AS5600 angle for telescope

    // Computed linear values
    float extensionMM;       // telescope linear extension
    float ropeLengthMM;      // calibrated rope: load-cell end to the knot (N20 encoder)
    float ropeTotalMM;       // ropeLengthMM + fixed path + knot-to-hook (see ROPE_* above)
    float hookDropMM;        // load-cell end to hook tip (pendulum length for sway)

    // Load
    float loadCellRaw;       // kg — HX711 reading (tare + scale), NOT angle-compensated
    float actualLoadKg;      // kg — boom-angle-compensated (Phase 3: same as loadCellRaw for now)

    // IMU orientation (complementary-filtered, zeroed by CAL1)
    float imuRoll;           // degrees — boom angle as seen by the IMU (cross-check for the encoder)
    float imuPitch;          // degrees — sideways lean of the boom

    // Outrigger force sensors (raw ADC 0–4095)
    uint16_t fsr[4];

    // Safety status (Phase 3 defaults)
    float safeLoadLimit;     // kg — from load chart (default 5.0)
    float loadPercent;       // (actualLoad / safeLimit) × 100
    uint8_t alarmLevel;      // 0=OK, 1=WARN, 2=CRITICAL, 3=ESTOP

    // System health
    bool sensorsOK;          // true if all expected I2C devices respond
};

/**
 * Motor command — parsed from G-code, forwarded to Mega or DRV8833.
 */
struct MotorCommand {
    uint8_t  axis;       // 1=Swing, 2=Lift, 3=Tele, 4=Winch, 0=StopAxis
    uint16_t speed;      // 0–MOTOR_SPEED_MAX (steps/s; the winch only uses speed > 0, see WINCH_PWM)
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
    CAL_RESET_TELE  = 2,  // Reset telescope extension to 0 / set its scale + direction
    CAL_ENC_REF     = 4,  // CAL4 A<axis> V<value>: "this encoder axis is at <value> now"
    CAL_ENC_INVERT  = 5,  // CAL5 A<axis> I<0|1>:  flip an encoder axis direction
    CAL_ROPE_SCALE  = 6,  // CAL6 S<mm per count>: rope (winch) scale
    CAL_LOAD        = 7,  // CAL7 W<kg> record a weight / CAL7 CLEAR / CAL7 S<counts per kg>
    CAL_AXIS_SCALE  = 8   // CAL8 A<1-3> S<units per encoder rev> [I<0|1>]: swing / boom / telescope ratio
};

// Highest speed accepted in an M-command (steps/s, steppers). The winch runs at WINCH_PWM.
#define MOTOR_SPEED_MAX  2500

// Dead-man timeout: a motor keeps running only while its M-command keeps being
// repeated (dashboard: every 100 ms while held). Same value in mega_executor.ino.
#define MOTION_TIMEOUT_MS  400

// Load % thresholds (match the dashboard: amber at 80 %, red at 100 %)
#define ALARM_WARN_PERCENT      80.0f
#define ALARM_CRITICAL_PERCENT  100.0f

// ======================== GLOBAL RTOS HANDLES ========================
// Declared here, defined in esp32_main.ino
extern SemaphoreHandle_t sensorMutex;

#endif // SLI_CONFIG_H
