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
static const float G_MS2            = 9.80665f;

bool MPU6050Driver::configure() {
    return _bus->writeReg(_addr, REG_PWR_MGMT_1,   0x01) &&   // wake, clock = PLL gyro X
           _bus->writeReg(_addr, REG_SMPLRT_DIV,   0x00) &&
           _bus->writeReg(_addr, REG_CONFIG,       0x06) &&   // DLPF 5 Hz (accel ~19 ms delay)
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
    *ax = s16(0)  / ACCEL_LSB_PER_G * G_MS2;
    *ay = s16(2)  / ACCEL_LSB_PER_G * G_MS2;
    *az = s16(4)  / ACCEL_LSB_PER_G * G_MS2;
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

    // Step gate: behind the 5 Hz DLPF the gyro cannot change by more than
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

    // Non-blocking gyro calibration in progress: collect instead of filtering
    if (_calTarget) {
        _calSum[0] += gx; _calSum[1] += gy; _calSum[2] += gz;
        _calAccel[0] = ax; _calAccel[1] = ay; _calAccel[2] = az;
        if (++_calCount >= _calTarget) {
            finishCalibration(_calSum[0], _calSum[1], _calSum[2], _calCount,
                              _calAccel[0], _calAccel[1], _calAccel[2]);
            _calTarget = 0;
            _calDone   = true;
        }
        return;
    }

    _accelX = ax; _accelY = ay; _accelZ = az;
    _gyroX = gx - _gyroOffsetX;
    _gyroY = gy - _gyroOffsetY;
    _gyroZ = gz - _gyroOffsetZ;

    const uint32_t now = micros();
    float dt = (now - _lastUpdateUs) / 1000000.0f;
    _lastUpdateUs = now;
    if (dt <= 0.0f || dt > 0.1f) dt = 0.01f;

    // Accelerometer tilt (degrees)
    const float accelRoll  = atan2f(_accelY, _accelZ) * 180.0f / PI;
    const float accelPitch = atan2f(-_accelX, sqrtf(_accelY * _accelY + _accelZ * _accelZ)) * 180.0f / PI;

    // Complementary filter, wrap-safe: pull the gyro prediction toward the
    // accel angle by the SHORTEST way round. A plain weighted average of +179
    // and -179 gives ~+172 instead of 180, so a sensor sitting near +/-180 roll
    // (Z axis pointing down) swung wildly across the whole range.
    const float rollPred  = wrapDeg180(_roll  + _gyroX * dt);
    const float pitchPred = _pitch + _gyroY * dt;
    _roll  = wrapDeg180(rollPred + (1.0f - ALPHA) * wrapDeg180(accelRoll - rollPred));
    _pitch = pitchPred + (1.0f - ALPHA) * (accelPitch - pitchPred);
}

void MPU6050Driver::finishCalibration(float sx, float sy, float sz, uint16_t n,
                                      float ax, float ay, float az) {
    if (n == 0) return;
    _gyroOffsetX = sx / n;
    _gyroOffsetY = sy / n;
    _gyroOffsetZ = sz / n;
    // Seed the filter with the gravity vector so it doesn't slowly converge from 0
    _roll  = atan2f(ay, az) * 180.0f / PI;
    _pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * 180.0f / PI;
    _lastUpdateUs = micros();
}

void MPU6050Driver::startGyroCalibration(uint16_t samples) {
    _calSum[0] = _calSum[1] = _calSum[2] = 0.0f;
    _calCount  = 0;
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
    float sx = 0.0f, sy = 0.0f, sz = 0.0f;
    float ax = 0.0f, ay = 0.0f, az = 1.0f;
    uint16_t valid = 0;
    for (uint16_t i = 0; i < samples; i++) {
        float tax, tay, taz, gx, gy, gz;
        if (readSample(&tax, &tay, &taz, &gx, &gy, &gz) &&
            fabsf(gx) < 300.0f && fabsf(gy) < 300.0f && fabsf(gz) < 300.0f) {
            sx += gx; sy += gy; sz += gz;
            ax = tax; ay = tay; az = taz;
            valid++;
        }
        delay(2);
    }
    finishCalibration(sx, sy, sz, valid, ax, ay, az);
    _lastUpdateUs = micros();
}
