// ============================================================
//  AS5600 Driver — Implementation (software I2C)
// ============================================================
#include "as5600_driver.h"
#include "encoder_axis.h"   // EncoderAxis::wrapDiff / combineReads

bool AS5600Driver::begin() {
    _lastProbeMs = millis();
    bool found = false;
    // Three quick probes (~0.2 ms each); no delay between them, so a re-probe
    // from inside a task never sleeps.
    for (int retry = 0; retry < 3 && !found && _bus; retry++) {
        found = _bus->beginTransmission(AS5600_ADDR);
        _bus->endTransmission();
    }
    _connected = found;
    _missCount = 0;
    return found;
}

bool AS5600Driver::readRaw(uint16_t* out) {
    if (!_connected) {
        if (millis() - _lastProbeMs < 100 || !begin()) return false;
    }

    uint16_t r[3];
    uint8_t  n = 0;
    for (uint8_t i = 0; i < 3; i++) {
        if (readAngleOnce(&r[n])) n++;
    }

    if (n == 0) {
        if (++_missCount > MISS_LIMIT) _connected = false;
        return false;
    }
    _missCount = 0;

    // Best case: median / agreement of 2-3 reads
    if (EncoderAxis::combineReads(r, n, AGREE_COUNTS, out)) {
        _lastGoodRaw = *out;
        _hasLastGood = true;
        return true;
    }

    // Only one read succeeded: accept it if it's close to the last good value
    if (n == 1 && _hasLastGood) {
        const int32_t diff = EncoderAxis::wrapDiff((int32_t)r[0], (int32_t)_lastGoodRaw);
        if (diff > -SINGLE_READ_GATE && diff < SINGLE_READ_GATE) {
            *out = r[0];
            _lastGoodRaw = r[0];
            return true;
        }
    }
    return false;
}

bool AS5600Driver::readAngleOnce(uint16_t* out) {
    uint8_t b[2];
    _reads = _reads + 1;
    if (!readBytes(REG_RAW_ANGLE, b, 2)) {
        _fails = _fails + 1;
        return false;
    }
    // Top nibble of the high byte is always 0; anything else is a bus error
    // (0xFFFF is the classic "nobody pulled SDA low" floating read).
    if (b[0] & 0xF0) {
        _fails = _fails + 1;
        return false;
    }
    *out = ((uint16_t)b[0] << 8) | b[1];
    return true;
}

bool AS5600Driver::readMagnetStatus(AS5600MagnetStatus* out) {
    out->valid = false;
    if (!_connected) return false;

    uint8_t status = 0, agc = 0, mag[2] = {0, 0};
    if (!readBytes(REG_STATUS, &status, 1)) return false;
    if (!readBytes(REG_AGC, &agc, 1))       return false;
    if (!readBytes(REG_MAGNITUDE, mag, 2))  return false;

    out->detected  = (status & 0x20) != 0;
    out->tooWeak   = (status & 0x10) != 0;
    out->tooStrong = (status & 0x08) != 0;
    out->agc       = agc;
    out->magnitude = (((uint16_t)mag[0] << 8) | mag[1]) & 0x0FFF;
    out->valid     = true;
    return true;
}

bool AS5600Driver::readBytes(uint8_t reg, uint8_t* buf, uint8_t len) {
    return _bus && _bus->readRegs(AS5600_ADDR, reg, buf, len);
}
