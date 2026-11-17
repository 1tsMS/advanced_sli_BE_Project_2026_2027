// ============================================================
//  MPU6050 IMU Driver — Implementation (software I2C)
// ============================================================
#include "mpu6050_driver.h"
#include <math.h>

// Registers (MPU-6000/6050 register map rev 4.2)
static const uint8_t REG_SMPLRT_DIV   = 0x19;
static const uint8_t REG_CONFIG       = 0x1A;
static const uint8_t REG_GYRO_CONFIG  = 0x1B;
static const uint8_t REG_ACCEL_CONFIG = 0x1C;
static const uint8_t REG_ACCEL_XOUT_H = 0x3B;   // 14 bytes: accel XYZ, temp, gyro XYZ
static const uint8_t REG_PWR_MGMT_1   = 0x6B;
static const uint8_t REG_WHO_AM_I     = 0x75;

static const float ACCEL_LSB_PER_G  = 4096.0f;   // +/-8 g
static const float GYRO_LSB_PER_DPS = 65.5f;     // +/-500 deg/s

bool MPU6050Driver::configure() {
    return _bus->writeReg(_addr, REG_PWR_MGMT_1,   0x01) &&   // wake, clock = PLL gyro X
           _bus->writeReg(_addr, REG_SMPLRT_DIV,   0x00) &&
           _bus->writeReg(_addr, REG_CONFIG,       0x04) &&   // DLPF 21 Hz (~8.5 ms delay): anti-alias for 100 Hz
           _bus->writeReg(_addr, REG_GYRO_CONFIG,  0x08) &&   // +/-500 deg/s
           _bus->writeReg(_addr, REG_ACCEL_CONFIG, 0x10);     // +/-8 g
}

bool MPU6050Driver::begin() {
    if (!_bus) return false;
    const uint8_t addrs[2] = { 0x68, 0x69 };   // AD0 low / high
    for (uint8_t a : addrs) {
        uint8_t who = 0;
        // WHO_AM_I is 0x68 on genuine parts; clones report other values, so only
        // reject "no device" patterns
        if (_bus->readRegs(a, REG_WHO_AM_I, &who, 1) && who != 0x00 && who != 0xFF) {
            _addr = a;
            delay(10);
            if (configure()) {
                delay(50);                       // let the DLPF settle
                _connected = true;
                _lastUpdateUs = micros();
                return true;
            }
        }
    }
    _connected = false;
    return false;
}

bool MPU6050Driver::readSample(float* ax, float* ay, float* az, float* gx, float* gy, float* gz) {
    uint8_t b[14];
    if (!_bus->readRegs(_addr, REG_ACCEL_XOUT_H, b, sizeof(b))) return false;
    auto s16 = [&](int i) { return (int16_t)((b[i] << 8) | b[i + 1]); };
    *ax = s16(0)  / ACCEL_LSB_PER_G * IMU_G;
    *ay = s16(2)  / ACCEL_LSB_PER_G * IMU_G;
    *az = s16(4)  / ACCEL_LSB_PER_G * IMU_G;
    *gx = s16(8)  / GYRO_LSB_PER_DPS;
    *gy = s16(10) / GYRO_LSB_PER_DPS;
    *gz = s16(12) / GYRO_LSB_PER_DPS;
    return true;
}

void MPU6050Driver::update() {
    if (!_connected) {
        // Cheap re-init every 0.5 s (a few register writes, < 2 ms)
        if (millis() - _lastReconnectMs < 500) return;
        _lastReconnectMs = millis();
        if (configure()) {
            _connected = true;
            _reconnects++;
            _lastUpdateUs = micros();
        }
        return;
    }

    float ax, ay, az, gx, gy, gz;
    if (!readSample(&ax, &ay, &az, &gx, &gy, &gz)) {
        // A single noisy transfer shouldn't kill the IMU feed
        if (++_consecutiveFails >= 3) {
            _connected = false;
            _consecutiveFails = 0;
        }
        return;
    }
    _consecutiveFails = 0;

    // Plausibility check: noise can corrupt bytes in a transfer that still "succeeds".
    // The crane moves slowly, so far from 1 g or a huge rotation rate = garbage: skip it.
    const float g2 = ax * ax + ay * ay + az * az;
    if (g2 < 25.0f || g2 > 400.0f ||                                  // outside 0.5 g .. 2 g
        fabsf(gx) > 300.0f || fabsf(gy) > 300.0f || fabsf(gz) > 300.0f) {
        _rejected++;
        return;
    }

    // Step gate: behind the 21 Hz DLPF the gyro cannot change by more than
    // GYRO_STEP_MAX between 10 ms samples on a slow crane. A corrupted gyro byte
    // that still lands under 300 deg/s would otherwise be integrated into a
    // fake swing of tens of degrees. A new level that persists is accepted.
    if (_hasPrevGyro &&
        (fabsf(gx - _prevGx) > GYRO_STEP_MAX || fabsf(gy - _prevGy) > GYRO_STEP_MAX ||
         fabsf(gz - _prevGz) > GYRO_STEP_MAX)) {
        _rejected++;
        if (++_gyroStepRun < GYRO_STEP_RESYNC) return;
    }
    _gyroStepRun = 0;
    _prevGx = gx; _prevGy = gy; _prevGz = gz;
    _hasPrevGyro = true;

    // Non-blocking calibration in progress: collect instead of filtering
    if (_calTarget) {
        _cal.add(ax, ay, az, gx, gy, gz);
        if (_cal.n >= _calTarget) {
            finishCalibration(_cal);
            _calTarget = 0;
            _calDone   = true;
        }
        return;
    }

    const uint32_t now = micros();
    float dt = (now - _lastUpdateUs) / 1000000.0f;
    _lastUpdateUs = now;
    if (dt <= 0.0f || dt > 0.1f) dt = 0.01f;

    const Vec3 a = { ax * _accelScale, ay * _accelScale, az * _accelScale };
    const Vec3 g = { gx - _gyroOffsetX, gy - _gyroOffsetY, gz - _gyroOffsetZ };
    _rawRoll  = accelRollDeg(a);
    _rawPitch = accelPitchDeg(a);
    _samples++;
    _out = _filter.update(a, g, dt);
}

void MPU6050Driver::finishCalibration(const CalSums& c) {
    if (c.n == 0) return;
    _gyroOffsetX = (float)(c.g[0] / c.n);
    _gyroOffsetY = (float)(c.g[1] / c.n);
    _gyroOffsetZ = (float)(c.g[2] / c.n);
    const Vec3 mean = { (float)(c.a[0] / c.n), (float)(c.a[1] / c.n), (float)(c.a[2] / c.n) };
    const float len = vecLen(mean);
    // At rest the accelerometer must read exactly 1 g. Cheap MPU6050 clones read
    // 10-20 % off; with the scale left at 1.0 the filter's |a| gate (15 %) then
    // distrusts EVERY sample: the angle runs on the gyro alone and drifts, and the
    // boom-tip acceleration shows a constant ~1.7 m/s^2 at rest (bench, 2026-11-09).
    // Accept anything the plausibility check lets through (0.5-2 g).
    if (len > 0.5f * IMU_G && len < 2.0f * IMU_G) _accelScale = IMU_G / len;
    _filter.seed({ mean.x * _accelScale, mean.y * _accelScale, mean.z * _accelScale });
    _out = { _filter.roll(), _filter.pitch(), 0.0f, 0.0f };
    _lastUpdateUs = micros();
}

void MPU6050Driver::startGyroCalibration(uint16_t samples) {
    _cal       = CalSums();
    _calDone   = false;
    _calTarget = samples ? samples : 1;
}

bool MPU6050Driver::takeGyroCalibrationDone() {
    if (!_calDone) return false;
    _calDone = false;
    return true;
}

void MPU6050Driver::calibrateGyro(uint16_t samples) {
    if (!_connected) return;
    CalSums c;
    for (uint16_t i = 0; i < samples; i++) {
        float ax, ay, az, gx, gy, gz;
        if (readSample(&ax, &ay, &az, &gx, &gy, &gz) &&
            fabsf(gx) < 300.0f && fabsf(gy) < 300.0f && fabsf(gz) < 300.0f) {
            c.add(ax, ay, az, gx, gy, gz);
        }
        delay(2);
    }
    finishCalibration(c);
}
