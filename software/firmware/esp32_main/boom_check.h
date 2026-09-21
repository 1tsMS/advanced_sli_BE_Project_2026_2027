// ============================================================
//  Boom Angle Cross-check — IMU vs boom encoder
//
//  The boom angle used for the load chart comes from the IMU on the boom.
//  The boom AS5600 sits on the boom pivot shaft (1:1), so it should move
//  exactly with the boom. This class compares the two:
//
//    encoder movement since the reference pose  ==  IMU boom angle
//
//  * The reference pose is set when the IMU is zeroed (CAL1).
//  * The encoder's direction (+1 / -1) is not known in advance. It is
//    learned the first time the boom moves BOOM_CHECK_LEARN_DEG away from
//    the reference, and then persisted by the caller.
//  * If the two disagree by more than BOOM_CHECK_TOL_DEG for
//    BOOM_CHECK_HOLD_MS the result is MISMATCH: one of the two sensors
//    (or the mounting) can no longer be trusted.
//
//  Pure logic, no hardware access: unit-tested on a PC.
// ============================================================
#ifndef BOOM_CHECK_H
#define BOOM_CHECK_H

#include <stdint.h>

#define BOOM_CHECK_TOL_DEG      3.0f    // allowed difference between IMU and encoder
#define BOOM_CHECK_HOLD_MS      500     // it must persist this long (ignores brief filter lag)
#define BOOM_CHECK_LEARN_DEG    10.0f   // IMU must move this far from the reference to learn direction
#define BOOM_CHECK_MIN_ENC_DEG  5.0f    // ...and the encoder must have moved at least this much

enum BoomCheckResult : uint8_t {
    BOOM_CHECK_LEARNING = 0,   // direction not learned yet (boom still near the reference pose)
    BOOM_CHECK_OK       = 1,
    BOOM_CHECK_MISMATCH = 2
};

class BoomAngleCheck {
public:
    BoomAngleCheck() : _refDeg(0.0f), _sign(0), _learned(false), _bad(false), _badSince(0), _errDeg(0.0f) {}

    /** Restore persisted state (reference angle and learned direction: -1, 0 = unknown, +1). */
    void configure(float refDeg, int8_t sign);

    /** New reference pose (IMU zeroed). The direction is relearned on the next movement. */
    void setReference(float encoderDeg);

    /**
     * Compare one pair of readings.
     * @param nowMs       millisecond clock
     * @param encoderDeg  boom encoder angle, 0..360
     * @param imuBoomDeg  calibrated IMU boom angle (0 at the reference pose)
     */
    BoomCheckResult update(uint32_t nowMs, float encoderDeg, float imuBoomDeg);

    float  refDeg() const { return _refDeg; }
    int8_t sign() const { return _sign; }
    /** |expected - IMU| from the last update, degrees (0 while learning). */
    float  errorDeg() const { return _errDeg; }

    /** True once after the direction was learned, so the caller can persist it. */
    bool takeLearned() { bool l = _learned; _learned = false; return l; }

    /** Encoder difference to the reference, wrapped to -180..180. */
    static float wrap180(float deg);

private:
    float    _refDeg;
    int8_t   _sign;
    bool     _learned;
    bool     _bad;
    uint32_t _badSince;
    float    _errDeg;
};

#endif // BOOM_CHECK_H
