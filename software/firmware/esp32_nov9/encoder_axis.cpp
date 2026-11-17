// ============================================================
//  EncoderAxis — Implementation
// ============================================================
#include "encoder_axis.h"

int32_t EncoderAxis::wrapDiff(int32_t a, int32_t b) {
    int32_t d = (a - b) % COUNTS_PER_REV;
    if (d < -COUNTS_PER_REV / 2)  d += COUNTS_PER_REV;
    if (d >= COUNTS_PER_REV / 2)  d -= COUNTS_PER_REV;
    return d;
}

bool EncoderAxis::combineReads(const uint16_t* r, uint8_t n, int32_t agreeCounts, uint16_t* out) {
    if (n < 2) return false;
    // Work in signed offsets from the first read so the 4095 -> 0 wrap can't break the comparison
    const int32_t d1 = wrapDiff(r[1], r[0]);
    if (n == 2) {
        if (d1 > agreeCounts || d1 < -agreeCounts) return false;
        *out = r[0] % COUNTS_PER_REV;
        return true;
    }
    const int32_t d2 = wrapDiff(r[2], r[0]);
    int32_t med;                                                   // median of {0, d1, d2}
    if ((d1 <= 0 && 0 <= d2) || (d2 <= 0 && 0 <= d1))       med = 0;
    else if ((0 <= d1 && d1 <= d2) || (d2 <= d1 && d1 <= 0)) med = d1;
    else                                                     med = d2;
    *out = (uint16_t)(((int32_t)r[0] + med + COUNTS_PER_REV) % COUNTS_PER_REV);
    return true;
}

void EncoderAxis::configure(int8_t sign, uint16_t zeroRaw, float ref, float unitsPerRev) {
    _sign    = (sign < 0) ? -1 : 1;
    _zeroRaw = zeroRaw % COUNTS_PER_REV;
    _ref     = ref;
    if (unitsPerRev > 0.0f) _unitsPerRev = unitsPerRev;
}

bool EncoderAxis::update(uint16_t raw, uint32_t nowMs) {
    raw %= COUNTS_PER_REV;

    if (!_hasSample) {
        _hasSample    = true;
        _lastRaw      = raw;
        _lastAcceptMs = nowMs;
        _turns        = 0;
        _zeroTurns    = 0;
        return true;
    }

    const int32_t delta = wrapDiff(raw, _lastRaw);

    if (_mode == ENC_LINEAR) {
        // Rate-aware glitch gate. The time is measured from the last ACCEPTED
        // sample, not the previous loop: if a few I2C reads failed while the shaft
        // kept turning, the next good sample legitimately covers several cycles
        // of motion and must not be thrown away as a glitch (that lost counts).
        const uint32_t dtMs = nowMs - _lastAcceptMs;
        int32_t limit = MAX_COUNTS_PER_MS * (int32_t)(dtMs == 0 ? 1 : dtMs);
        if (limit > MAX_JUMP_COUNTS) limit = MAX_JUMP_COUNTS;

        if (delta > limit || delta < -limit) {
            _rejected++;
            if (++_rejectRun < RESYNC_AFTER) return false;
            // The new position is consistent: take it, but the jump itself is lost
            _resyncs++;
            _rejectRun    = 0;
            _lastRaw      = raw;
            _lastAcceptMs = nowMs;
            return true;
        }
        _turns += delta;
    }

    _rejectRun    = 0;
    _lastRaw      = raw;
    _lastAcceptMs = nowMs;
    return true;
}

int32_t EncoderAxis::rawCountsFromReference() const {
    if (!_hasSample) return 0;
    return (_mode == ENC_ROTARY) ? wrapDiff(_lastRaw, _zeroRaw) : (_turns - _zeroTurns);
}

void EncoderAxis::restore(float ref, int32_t savedRawCounts, uint16_t savedRaw) {
    _ref = ref;
    if (!_hasSample) return;
    // Counts the shaft moved while powered off (assumed < half a turn)
    const int32_t drift = wrapDiff(_lastRaw, savedRaw);
    if (_mode == ENC_ROTARY) {
        _zeroRaw = (uint16_t)(((int32_t)_lastRaw - (savedRawCounts + drift)) & (COUNTS_PER_REV - 1));
    } else {
        _zeroTurns = _turns - (savedRawCounts + drift);
    }
}

float EncoderAxis::value() const {
    if (!_hasSample) return _ref;
    return _ref + (float)countsFromReference() * _unitsPerRev / (float)COUNTS_PER_REV;
}

int32_t EncoderAxis::countsFromReference() const {
    return _sign * rawCountsFromReference();
}

void EncoderAxis::setReference(float value) {
    _ref       = value;
    _zeroRaw   = _lastRaw;
    _zeroTurns = _turns;
}

void EncoderAxis::setInvert(bool invert) {
    _sign = invert ? -1 : 1;
}
