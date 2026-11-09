// ============================================================
//  MPU6050 6-Axis IMU Driver (software I2C, bus 0: GPIO 21/22)
//
//  Talks to the chip directly over SoftI2C instead of Adafruit_MPU6050,
//  because that library needs the hardware Wire driver, which blocks for
//  >= 1 s on a noise-hung transfer in this ESP32 core.
//
//    Accelerometer: +/-8 g     Gyroscope: +/-500 deg/s
//    DLPF: 21 Hz = anti-alias filter for the 100 Hz sampling. It keeps the
//    boom-tip shocks (stop, load lift-off) visible; the angle is smoothed in
//    software instead (imu_filter.h: median 3 -> gated complementary filter).
// ============================================================
#ifndef MPU6050_DRIVER_H
#define MPU6050_DRIVER_H

#include "config.h"
#include "soft_i2c.h"
#include "imu_filter.h"

class MPU6050Driver {
public:
    explicit MPU6050Driver(SoftI2C* bus) : _bus(bus) {}

    /** Probe 0x68 / 0x69, check WHO_AM_I and configure. Call in setup(). */
    bool begin();

    bool isConnected() const { return _connected; }

    /** Read one sample and update the filter. Call at 100 Hz. */
    void update();

    /** Average gyro + accel at rest: gyro zero offset, 1 g accel scale, filter
     *  seed. BLOCKING (~2 ms per sample): only for setup(), before the tasks run. */
    void calibrateGyro(uint16_t samples = 200);

    /** Non-blocking version for use inside a task: the next `samples` calls to
     *  update() collect gyro samples instead of filtering (~1 s at 100 Hz). */
    void startGyroCalibration(uint16_t samples);
    /** True exactly once when a started calibration has finished. */
    bool takeGyroCalibrationDone();

    // Filtered orientation in degrees (raw sensor frame, no zero offset)
    float getRoll()  const { return _out.rollDeg; }
    float getPitch() const { return _out.pitchDeg; }
    // Boom-tip motion of the last sample: acceleration beyond gravity (m/s^2) and jerk (m/s^3)
    float getAccelDyn() const { return _out.accelDyn; }
    float getJerk()     const { return _out.jerk; }

    // Unfiltered tilt of the LAST accepted sample: accelerometer only, scale-corrected,
    // before the median and the gate (E1: what the EMI does to the raw signal).
    float getRawRoll()  const { return _rawRoll; }
    float getRawPitch() const { return _rawPitch; }
    /** Bumped once per sample that reached the filter (not rejected / calibrating). */
    uint32_t sampleCount() const { return _samples; }

    uint32_t reconnects()      const { return _reconnects; }
    uint32_t rejectedSamples() const { return _rejected; }
    /** 1 g / |accel at rest| from the last calibration (1.0 = never calibrated). */
    float    accelScale()      const { return _accelScale; }

private:
    SoftI2C* _bus;
    uint8_t  _addr = 0x68;
    bool     _connected = false;

    ImuFilter       _filter;
    ImuFilterOutput _out = {};
    float _gyroOffsetX = 0.0f, _gyroOffsetY = 0.0f, _gyroOffsetZ = 0.0f;
    float _accelScale = 1.0f;     // 1 g / |accel at rest| (MPU6050 scale error is a few %)
    uint32_t _lastUpdateUs = 0;

    uint32_t _lastReconnectMs  = 0;
    uint8_t  _consecutiveFails = 0;
    uint32_t _reconnects = 0;     // re-inits after dropouts
    uint32_t _rejected   = 0;     // implausible (corrupted) samples skipped
    float    _rawRoll = 0.0f, _rawPitch = 0.0f;
    uint32_t _samples = 0;

    // Gyro step gate (raw deg/s of the last accepted sample)
    static constexpr float   GYRO_STEP_MAX    = 100.0f;   // deg/s change per sample
    static constexpr uint8_t GYRO_STEP_RESYNC = 5;        // consistent samples -> accept
    float   _prevGx = 0.0f, _prevGy = 0.0f, _prevGz = 0.0f;
    bool    _hasPrevGyro = false;
    uint8_t _gyroStepRun = 0;

    // Calibration sums (gyro, accel): the accel MEAN seeds the filter and sets
    // the 1 g scale (one noisy sample used to set the zero, up to 1-2 deg off)
    struct CalSums {
        double g[3] = {0, 0, 0};
        double a[3] = {0, 0, 0};
        uint16_t n = 0;
        void add(float ax, float ay, float az, float gx, float gy, float gz) {
            a[0] += ax; a[1] += ay; a[2] += az; g[0] += gx; g[1] += gy; g[2] += gz; n++;
        }
    };
    CalSums  _cal;
    uint16_t _calTarget = 0;
    bool     _calDone = false;

    void finishCalibration(const CalSums& c);

    bool configure();
    /** Read accel + gyro (raw sensor units -> m/s^2, deg/s without offsets). */
    bool readSample(float* ax, float* ay, float* az, float* gx, float* gy, float* gz);
};

#endif // MPU6050_DRIVER_H
