// ============================================================
//  Boom-tip IMU filter: pure maths, no Arduino (unit-tested on the PC in
//  host_tests/test_imu_filter.cpp). MPU6050Driver feeds it one sample per 10 ms.
//
//  Pipeline per sample:
//    1. Median of the last 3 samples, per axis (accel and gyro).
//       The noise on this crane is IMPULSIVE: a stepper edge corrupts one I2C
//       transfer and one sample jumps. A mean or exponential average smears
//       that spike over the next samples; a median of 3 removes it completely
//       and costs one sample (10 ms) of delay. It keeps real steps.
//    2. Complementary tilt filter, gated by |a|.
//       The gyro tracks motion without lag; the accelerometer pulls the angle
//       back to gravity with time constant tauS (no drift). While the boom
//       jerks (stop, load lift-off) the accelerometer measures more than
//       gravity, so its pull fades out when | |a| - 1 g | grows and is off
//       above gateG. A plain complementary filter tilts the angle at every jerk.
//       alpha is computed from the real dt, so a late sample doesn't change tau.
//    3. Dynamic acceleration and jerk at the boom tip.
//       accelDyn = |a - g|, with the gravity vector g rebuilt from the filtered
//       roll/pitch (tracks rotation with no lag, unlike a high-pass filter).
//       jerk = |a[k] - a[k-1]| / dt (gravity cancels in the difference).
// ============================================================
#ifndef IMU_FILTER_H
#define IMU_FILTER_H

#include <math.h>
#include <stdint.h>

static const float IMU_G   = 9.80665f;
static const float IMU_RAD = 57.29577951f;   // deg per rad

/** Wrap an angle into (-180, 180]. Roll from atan2 jumps +180 <-> -180, so any
 *  blend or difference of angles must go through this. */
inline float wrapDeg180(float a) {
    while (a > 180.0f)   a -= 360.0f;
    while (a <= -180.0f) a += 360.0f;
    return a;
}

/** Median of the last three values pushed (fewer at start: the newest). */
struct Median3 {
    float   v[3] = {0.0f, 0.0f, 0.0f};
    uint8_t n    = 0;

    float push(float x) {
        v[0] = v[1]; v[1] = v[2]; v[2] = x;
        if (n < 3) n++;
        if (n < 3) return x;
        const float a = v[0], b = v[1], c = v[2];
        if (a > b) return (b > c) ? b : (a > c ? c : a);
        return (a > c) ? a : (b > c ? c : b);
    }
    void reset() { n = 0; }
};

struct Vec3 { float x, y, z; };

inline float vecLen(const Vec3& a) { return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }

/** Roll / pitch (deg) of the gravity vector measured by the accelerometer. */
inline float accelRollDeg(const Vec3& a)  { return atan2f(a.y, a.z) * IMU_RAD; }
inline float accelPitchDeg(const Vec3& a) { return atan2f(-a.x, sqrtf(a.y * a.y + a.z * a.z)) * IMU_RAD; }

/** Gravity in the sensor frame for a roll / pitch (inverse of the two functions above). */
inline Vec3 gravityAt(float rollDeg, float pitchDeg) {
    const float r = rollDeg / IMU_RAD, p = pitchDeg / IMU_RAD;
    return { -IMU_G * sinf(p), IMU_G * cosf(p) * sinf(r), IMU_G * cosf(p) * cosf(r) };
}

/** Accelerometer trust (0..1) for a measured |a|: 1 at exactly 1 g, 0 at gateG off. */
inline float accelTrust(float accLen, float gateG) {
    const float err = fabsf(accLen / IMU_G - 1.0f);
    return err >= gateG ? 0.0f : 1.0f - err / gateG;
}

struct ImuFilterOutput {
    float rollDeg, pitchDeg;   // filtered tilt, raw sensor frame (no zero offset)
    float accelDyn;            // m/s^2, acceleration beyond gravity
    float jerk;                // m/s^3
};

class ImuFilter {
public:
    float tauS  = 1.0f;     // accel correction time constant (s)
    float gateG = 0.15f;    // accel ignored when | |a|/g - 1 | >= this

    /** Start from a gravity vector at rest (no slow convergence from 0). */
    void seed(const Vec3& a) {
        _roll  = accelRollDeg(a);
        _pitch = accelPitchDeg(a);
        _prevA = a;
        _hasPrev = true;
        for (auto& m : _medA) m.reset();
        for (auto& m : _medG) m.reset();
    }

    /** One sample: accel in m/s^2 (scale-corrected), gyro in deg/s (bias removed), dt in s. */
    ImuFilterOutput update(const Vec3& accRaw, const Vec3& gyroRaw, float dt) {
        const Vec3 a = { _medA[0].push(accRaw.x), _medA[1].push(accRaw.y), _medA[2].push(accRaw.z) };
        const Vec3 g = { _medG[0].push(gyroRaw.x), _medG[1].push(gyroRaw.y), _medG[2].push(gyroRaw.z) };

        // Complementary filter. Roll is wrap-safe: blend the shortest way round
        // (a plain average of +179 and -179 gives ~0 instead of 180).
        const float k  = (dt / (tauS + dt)) * accelTrust(vecLen(a), gateG);
        const float rollPred  = wrapDeg180(_roll + g.x * dt);
        const float pitchPred = _pitch + g.y * dt;
        _roll  = wrapDeg180(rollPred + k * wrapDeg180(accelRollDeg(a) - rollPred));
        _pitch = pitchPred + k * (accelPitchDeg(a) - pitchPred);

        // Dynamic acceleration and jerk
        const Vec3 grav = gravityAt(_roll, _pitch);
        const Vec3 dyn  = { a.x - grav.x, a.y - grav.y, a.z - grav.z };
        float jerk = 0.0f;
        if (_hasPrev && dt > 0.0f) {
            const Vec3 d = { a.x - _prevA.x, a.y - _prevA.y, a.z - _prevA.z };
            jerk = vecLen(d) / dt;
        }
        _prevA = a;
        _hasPrev = true;
        return { _roll, _pitch, vecLen(dyn), jerk };
    }

    float roll()  const { return _roll; }
    float pitch() const { return _pitch; }

private:
    float   _roll = 0.0f, _pitch = 0.0f;
    Vec3    _prevA = {0.0f, 0.0f, IMU_G};
    bool    _hasPrev = false;
    Median3 _medA[3], _medG[3];
};

#endif // IMU_FILTER_H
