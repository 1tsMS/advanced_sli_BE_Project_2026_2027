// ============================================================
//  MPU6050 6-Axis IMU Driver (software I2C, bus 0: GPIO 21/22)
//
//  Talks to the chip directly over SoftI2C instead of Adafruit_MPU6050,
//  because that library needs the hardware Wire driver, which blocks for
//  >= 1 s on a noise-hung transfer in this ESP32 core.
//
//    Accelerometer: +/-8 g     Gyroscope: +/-500 deg/s
//    DLPF: 5 Hz (cuts stepper vibration before the complementary filter)
//
//  Roll/pitch come from a complementary filter (gyro integration + accel tilt).
// ============================================================
#ifndef MPU6050_DRIVER_H
#define MPU6050_DRIVER_H

#include "config.h"
#include "soft_i2c.h"

/** Wrap an angle into (-180, 180]. Roll from atan2 jumps +180 <-> -180, so any
 *  blend or difference of angles must go through this. */
inline float wrapDeg180(float a) {
    while (a > 180.0f)   a -= 360.0f;
    while (a <= -180.0f) a += 360.0f;
    return a;
}

class MPU6050Driver {
public:
    explicit MPU6050Driver(SoftI2C* bus) : _bus(bus) {}

    /** Probe 0x68 / 0x69, check WHO_AM_I and configure. Call in setup(). */
    bool begin();

    bool isConnected() const { return _connected; }

    /** Read one sample and update the complementary filter. Call at 100 Hz. */
    void update();

    /** Average the gyro at rest to find its zero offset. BLOCKING (~2 ms per
     *  sample): only for setup(), before the tasks run. */
    void calibrateGyro(uint16_t samples = 200);

    /** Non-blocking version for use inside a task: the next `samples` calls to
     *  update() collect gyro samples instead of filtering (~1 s at 100 Hz). */
    void startGyroCalibration(uint16_t samples);
    /** True exactly once when a started calibration has finished. */
    bool takeGyroCalibrationDone();

    // Filtered orientation in degrees
    float getRoll()  const { return _roll; }
    float getPitch() const { return _pitch; }

    // Latest sample: accelerometer in m/s^2, gyro in deg/s (zero offset removed)
    float getAccelX() const { return _accelX; }
    float getAccelY() const { return _accelY; }
    float getAccelZ() const { return _accelZ; }
    float getGyroX()  const { return _gyroX; }
    float getGyroY()  const { return _gyroY; }
    float getGyroZ()  const { return _gyroZ; }

    uint32_t reconnects()      const { return _reconnects; }
    uint32_t rejectedSamples() const { return _rejected; }

private:
    SoftI2C* _bus;
    uint8_t  _addr = 0x68;
    bool     _connected = false;

    float _roll = 0.0f, _pitch = 0.0f;
    float _accelX = 0.0f, _accelY = 0.0f, _accelZ = 0.0f;   // m/s^2
    float _gyroX = 0.0f, _gyroY = 0.0f, _gyroZ = 0.0f;      // deg/s
    float _gyroOffsetX = 0.0f, _gyroOffsetY = 0.0f, _gyroOffsetZ = 0.0f;
    uint32_t _lastUpdateUs = 0;

    uint32_t _lastReconnectMs  = 0;
    uint8_t  _consecutiveFails = 0;
    uint32_t _reconnects = 0;     // re-inits after dropouts
    uint32_t _rejected   = 0;     // implausible (corrupted) samples skipped

    // Gyro step gate (raw deg/s of the last accepted sample)
    static constexpr float   GYRO_STEP_MAX    = 100.0f;   // deg/s change per sample
    static constexpr uint8_t GYRO_STEP_RESYNC = 5;        // consistent samples -> accept
    float   _prevGx = 0.0f, _prevGy = 0.0f, _prevGz = 0.0f;
    bool    _hasPrevGyro = false;
    uint8_t _gyroStepRun = 0;

    // Non-blocking gyro calibration state
    uint16_t _calTarget = 0, _calCount = 0;
    float    _calSum[3] = {0, 0, 0};
    float    _calAccel[3] = {0, 0, 0};
    bool     _calDone = false;

    void finishCalibration(float sx, float sy, float sz, uint16_t n, float ax, float ay, float az);

    // Complementary filter weight. tau = dt * a / (1 - a): 0.99 @ 100 Hz ~ 1 s.
    // Halves the accelerometer jitter (stepper vibration) vs 0.98 (0.5 s); the
    // cost is ~1 s for the angle to settle after the boom stops. Motion itself
    // is tracked by the gyro without that lag.
    static constexpr float ALPHA = 0.99f;

    bool configure();
    /** Read accel + gyro (raw sensor units -> m/s^2, deg/s without offsets). */
    bool readSample(float* ax, float* ay, float* az, float* gx, float* gy, float* gz);
};

#endif // MPU6050_DRIVER_H
