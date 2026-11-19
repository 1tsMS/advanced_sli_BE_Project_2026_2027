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

// Values written by configure(); configMatches() compares only these fields
static const uint8_t CFG_CONFIG = 0x04;   // DLPF 21 Hz (~8.5 ms delay): anti-alias for 100 Hz
static const uint8_t CFG_GYRO   = 0x08;   // FS_SEL 1: +/-500 deg/s
static const uint8_t CFG_ACCEL  = 0x10;   // AFS_SEL 2: +/-8 g

/**
 * Do registers 0x19..0x1C (SMPLRT_DIV, CONFIG, GYRO_CONFIG, ACCEL_CONFIG) hold
 * what configure() wrote? Only the fields we set are compared (DLPF, full-scale
 * ranges), so a clone's reserved bits can't fail it. false on a failed read
 * (and *readOk = false, when given).
 */
bool MPU6050Driver::configMatches(bool* readOk) {
    uint8_t r[4];
    const bool ok = _bus->readRegs(_addr, REG_SMPLRT_DIV, r, sizeof(r));
    if (readOk) *readOk = ok;
    if (!ok) return false;
    return r[0] == 0x00 &&
           (r[1] & 0x07) == CFG_CONFIG &&
           (r[2] & 0x18) == CFG_GYRO &&
           (r[3] & 0x18) == CFG_ACCEL;
}

bool MPU6050Driver::configure() {
    // Written, then READ BACK (MS, 19 Nov): a write that ACKs can still land wrong
    // under EMI. A wrong full-scale range silently mis-scales every sample: the
    // accel leaves the 1 g gate (no correction) and the gyro bias no longer
    // matches, so the boom angle spun at -3.5 deg/s while the raw angle was right.
    // A failed check just returns false: update() writes again 100 ms later.
    return _bus->writeReg(_addr, REG_PWR_MGMT_1,   0x01) &&   // wake, clock = PLL gyro X
           _bus->writeReg(_addr, REG_SMPLRT_DIV,   0x00) &&
           _bus->writeReg(_addr, REG_CONFIG,       CFG_CONFIG) &&
           _bus->writeReg(_addr, REG_GYRO_CONFIG,  CFG_GYRO) &&
           _bus->writeReg(_addr, REG_ACCEL_CONFIG, CFG_ACCEL) &&
           configMatches();
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
        // Cheap re-init every 100 ms (a few register writes, < 2 ms). Was 500 ms:
        // each EMI burst became >= 0.5 s of FAULT on the crane (MS, 18 Nov).
        if (millis() - _lastReconnectMs < 100) return;
        _lastReconnectMs = millis();
        if (configure()) {
            _connected = true;
            _reconnects++;
            _lastUpdateUs = micros();
            _lastCfgCheckMs = millis();
        }
        return;
    }

    // Once a second: are the ranges still what we set? (A read that fails proves
    // nothing and is ignored; a read that shows other ranges forces a re-init.)
    if (millis() - _lastCfgCheckMs >= CFG_CHECK_MS) {
        _lastCfgCheckMs = millis();
        bool readOk;
        if (!configMatches(&readOk) && readOk) {
            _cfgFaults++;
            _connected = false;
            return;
        }
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
    gravityGuard(a);
}

void MPU6050Driver::gravityGuard(const Vec3& a) {
    // On a slow crane |a| is ~1 g except for short jerks. Off by more than the
    // filter's gate for 2 s = the accel is mis-scaled (wrong range, bad calibration)
    // and the angle is running on the gyro alone: stop trusting it (SLI -> FAULT).
    const bool inBand = accelTrust(vecLen(a), GRAVITY_BAND) > 0.0f;
    if (_trusted) {
        _offGRun = inBand ? 0 : (uint16_t)(_offGRun + 1);
        if (_offGRun >= GRAVITY_TRIP_SAMPLES) {
            _trusted = false;
            _gravityFaults++;
            _recoverSum = { 0.0f, 0.0f, 0.0f };
            _recoverN = 0;
        }
        return;
    }
    // Untrusted: wait for a run of good samples, then re-seed the angle from the
    // measured gravity so it snaps back to the truth instead of a 1 s catch-up.
    if (!inBand) { _recoverN = 0; _recoverSum = { 0.0f, 0.0f, 0.0f }; return; }
    _recoverSum.x += a.x; _recoverSum.y += a.y; _recoverSum.z += a.z;
    if (++_recoverN >= GRAVITY_RECOVER_SAMPLES) {
        const float n = (float)_recoverN;
        _filter.seed({ _recoverSum.x / n, _recoverSum.y / n, _recoverSum.z / n });
        _out = { _filter.roll(), _filter.pitch(), 0.0f, 0.0f };
        _trusted = true;
        _offGRun = 0;
    }
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
