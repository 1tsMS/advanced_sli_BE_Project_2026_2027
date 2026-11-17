// ============================================================
//  EncoderAxis — turns raw AS5600 counts into a calibrated axis value
//
//  Pure logic (no Arduino / RTOS calls) so it can be unit-tested on a PC.
//
//  Two modes:
//    ROTARY  (swing, boom): the AS5600 is absolute within one turn, so the
//            value survives a reboot. value = ref + sign * wrap(raw - zeroRaw)
//            in units of unitsPerRev (360 for a 1:1 shaft), wrapped to
//            +/- half a turn around the zero.
//    LINEAR  (telescope): the shaft turns many times, so wraps are counted.
//            value = ref + sign * (turnCounts - zeroCounts) * unitsPerRev / 4096.
//            The turn count is lost on reboot, so the value starts at `ref`
//            at power-up (power up with the telescope retracted).
//
//  Calibration:
//    setReference(v)  -> "the axis is at v right now" (re-anchors at the current position)
//    setInvert(inv)   -> flip the direction around the existing reference point
//                        (a reading of +30 becomes -30; the zero stays where it was)
// ============================================================
#ifndef ENCODER_AXIS_H
#define ENCODER_AXIS_H

#include <stdint.h>

enum EncoderMode : uint8_t { ENC_ROTARY = 0, ENC_LINEAR = 1 };

class EncoderAxis {
public:
    static const int32_t COUNTS_PER_REV = 4096;

    // A reading that implies a faster shaft than this is treated as a glitch.
    // 51 counts/ms = 4.5 deg/ms = 750 RPM, far above anything a NEMA17 here does.
    static const int32_t MAX_COUNTS_PER_MS = 51;
    // Never accept a jump of half a turn or more: the direction would be ambiguous.
    static const int32_t MAX_JUMP_COUNTS = 1800;
    // After this many glitch rejections in a row, accept the new position
    // without counting the jump (stops the axis freezing; counted as a resync).
    static const uint8_t RESYNC_AFTER = 5;

    EncoderAxis(EncoderMode mode, float unitsPerRev)
        : _mode(mode), _unitsPerRev(unitsPerRev) {}

    /** Load persisted calibration (call before the first update). */
    void configure(int8_t sign, uint16_t zeroRaw, float ref, float unitsPerRev);

    /**
     * Feed one raw reading (0..4095).
     * @param nowMs current time (millis); the glitch gate uses the time since
     *              the last ACCEPTED sample, so failed reads don't shrink it
     * @return false if the reading was rejected as a glitch
     */
    bool update(uint16_t raw, uint32_t nowMs);

    /** Encoder counts since the reference point, before the direction sign (for saving). */
    int32_t rawCountsFromReference() const;

    /**
     * Restore a saved position after a reboot, keeping the original reference
     * point (so invert / scale changes still act around the true zero).
     * Call after the first sample. Assumes the shaft moved < half a turn while off.
     * @param ref            saved reference value
     * @param savedRawCounts saved rawCountsFromReference()
     * @param savedRaw       raw angle at the time of saving
     */
    void restore(float ref, int32_t savedRawCounts, uint16_t savedRaw);

    /** Calibrated value: degrees (rotary) or mm (linear). 0 before the first sample. */
    float value() const;

    /** Signed encoder counts moved since the reference point (direction-corrected). */
    int32_t countsFromReference() const;

    void setReference(float value);
    void setInvert(bool invert);
    void setUnitsPerRev(float unitsPerRev) { if (unitsPerRev > 0.0f) _unitsPerRev = unitsPerRev; }

    bool     hasSample()   const { return _hasSample; }
    int8_t   sign()        const { return _sign; }
    bool     inverted()    const { return _sign < 0; }
    uint16_t zeroRaw()     const { return _zeroRaw; }
    float    reference()   const { return _ref; }
    float    unitsPerRev() const { return _unitsPerRev; }
    uint32_t rejected()    const { return _rejected; }
    uint32_t resyncs()     const { return _resyncs; }
    uint16_t lastRaw()     const { return _lastRaw; }
    int32_t  turns()       const { return _turns; }
    int32_t  zeroTurns()   const { return _zeroTurns; }
    EncoderMode mode()     const { return _mode; }

    /** Shortest signed difference a - b in counts, in [-2048, 2047]. */
    static int32_t wrapDiff(int32_t a, int32_t b);

    /**
     * Combine n (0..3) back-to-back raw reads into one trustworthy value:
     * 3 reads -> wrap-aware median (one corrupted transfer is outvoted),
     * 2 reads -> accepted only if they agree within agreeCounts,
     * 0-1     -> rejected (nothing to cross-check against).
     */
    static bool combineReads(const uint16_t* r, uint8_t n, int32_t agreeCounts, uint16_t* out);

private:
    EncoderMode _mode;
    float    _unitsPerRev;
    int8_t   _sign      = 1;
    uint16_t _zeroRaw   = 0;      // rotary: raw count at the reference point
    float    _ref       = 0.0f;   // value at the reference point
    int32_t  _zeroTurns = 0;      // linear: _turns at the reference point

    bool     _hasSample = false;
    uint32_t _lastAcceptMs = 0;
    uint16_t _lastRaw   = 0;
    int32_t  _turns     = 0;      // linear: accumulated counts (multi-turn)
    uint8_t  _rejectRun = 0;
    uint32_t _rejected  = 0;
    uint32_t _resyncs   = 0;
};

#endif // ENCODER_AXIS_H
