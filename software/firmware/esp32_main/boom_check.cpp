// ============================================================
//  Boom Angle Cross-check — Implementation
// ============================================================
#include "boom_check.h"
#include <math.h>

float BoomAngleCheck::wrap180(float deg) {
    deg = fmodf(deg, 360.0f);
    if (deg > 180.0f)  deg -= 360.0f;
    if (deg <= -180.0f) deg += 360.0f;
    return deg;
}

void BoomAngleCheck::configure(float refDeg, int8_t sign) {
    _refDeg   = refDeg;
    _sign     = (sign > 0) ? 1 : (sign < 0 ? -1 : 0);   // never trust a corrupt value
    _learned  = false;
    _bad      = false;
    _errDeg   = 0.0f;
}

void BoomAngleCheck::setReference(float encoderDeg) {
    _refDeg  = encoderDeg;
    _sign    = 0;            // relearn the direction
    _learned = false;
    _bad     = false;
    _errDeg  = 0.0f;
}

BoomCheckResult BoomAngleCheck::update(uint32_t nowMs, float encoderDeg, float imuBoomDeg) {
    const float delta = wrap180(encoderDeg - _refDeg);
    bool candidate = false;   // this reading disagrees

    if (_sign == 0) {
        if (fabsf(imuBoomDeg) < BOOM_CHECK_LEARN_DEG) {
            _bad = false;
            _errDeg = 0.0f;
            return BOOM_CHECK_LEARNING;          // boom still near the reference: nothing to learn from yet
        }
        if (fabsf(delta) >= BOOM_CHECK_MIN_ENC_DEG) {
            _sign    = (delta * imuBoomDeg > 0.0f) ? 1 : -1;
            _learned = true;
        } else {
            // The IMU says the boom moved a long way but the encoder did not follow
            candidate = true;
            _errDeg   = fabsf(imuBoomDeg);
        }
    }

    if (_sign != 0) {
        _errDeg   = fabsf(_sign * delta - imuBoomDeg);
        candidate = (_errDeg > BOOM_CHECK_TOL_DEG);
    }

    if (!candidate) {
        _bad = false;
        return _sign == 0 ? BOOM_CHECK_LEARNING : BOOM_CHECK_OK;
    }

    // Only report a mismatch once it has lasted (brief disagreement is filter lag)
    if (!_bad) {
        _bad      = true;
        _badSince = nowMs;
    }
    return (nowMs - _badSince >= (uint32_t)BOOM_CHECK_HOLD_MS) ? BOOM_CHECK_MISMATCH
                                                               : (_sign == 0 ? BOOM_CHECK_LEARNING : BOOM_CHECK_OK);
}
