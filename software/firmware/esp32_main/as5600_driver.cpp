// ============================================================
//  AS5600 Driver — Implementation (Matches 3_AS5600_test.ino)
// ============================================================
#include "as5600_driver.h"
#include "soft_i2c.h"

bool AS5600Driver::isConnected() {
    if (_connected) return true;
    return begin();
}

bool AS5600Driver::begin() {
    // Probing mirrors detectAS5600 in 3_AS5600_test.ino with 3 retries
    for (int retry = 0; retry < 3; retry++) {
        switch (_busType) {
            case BUS_WIRE0:
                Wire.setClock(100000);
                Wire.beginTransmission(AS5600_ADDR);
                _connected = (Wire.endTransmission() == 0);
                break;
            case BUS_WIRE1:
                Wire1.setClock(100000);
                Wire1.beginTransmission(AS5600_ADDR);
                _connected = (Wire1.endTransmission() == 0);
                break;
            case BUS_SOFT:
                if (_softBus) {
                    _connected = _softBus->beginTransmission(AS5600_ADDR);
                    _softBus->endTransmission();
                }
                break;
        }
        if (_connected) break;
        delay(5);
    }
    return _connected;
}

float AS5600Driver::readAngle() {
    uint16_t raw = readRawAngle();
    // Apply zero offset correction
    int16_t corrected = (int16_t)raw - (int16_t)_offset;
    if (corrected < 0) corrected += 4096;
    float currentAngle = (corrected / 4096.0f) * 360.0f;

    if (_isFirstRead) {
        _lastAngle = currentAngle;
        _isFirstRead = false;
        return currentAngle;
    }

    // Shortest angular difference across 0/360 boundary
    float delta = currentAngle - _lastAngle;
    if (delta < -180.0f) {
        delta += 360.0f;
    } else if (delta > 180.0f) {
        delta -= 360.0f;
    }

    // Glitch rejection filter:
    // Sensor task runs at 100Hz (every 10ms).
    // Physical stepper motor cannot rotate > 45° in 10ms (~750 RPM).
    // Discard any impossible spike caused by I2C electrical noise.
    if (fabs(delta) > 45.0f) {
        return _lastAngle;
    }

    _continuousAngle += delta;
    _lastAngle = currentAngle;
    return currentAngle;
}

float AS5600Driver::readContinuousAngle() {
    readAngle(); // Updates continuousAngle
    return _continuousAngle;
}

uint16_t AS5600Driver::readRawAngle() {
    uint16_t value = 0;
    // Read REG_ANGLE (0x0E) which uses AS5600's internal hardware digital filter
    if (readRegister16(REG_ANGLE, &value)) {
        _lastRawAngle = value;
        _missCount = 0;
        _connected = true;
        return value;
    } else {
        if (++_missCount > 30) {
            _connected = false;
        }
        return _lastRawAngle;
    }
}

void AS5600Driver::setZero() {
    _offset = readRawAngle();
    _lastAngle = 0.0f;
    _continuousAngle = 0.0f;
    _isFirstRead = true;
}

bool AS5600Driver::readRegister16(uint8_t reg, uint16_t* outValue) {
    // Each bus type uses its own Wire object.
    // All AS5600s have address 0x36 — separated by bus, not address.

    switch (_busType) {
        case BUS_WIRE0: {
            Wire.setClock(100000);  // Ensure Wire stays at 100kHz
            Wire.beginTransmission(AS5600_ADDR);
            Wire.write(reg);
            uint8_t err = Wire.endTransmission(false);  // Repeated start
            if (err != 0) {
                // If repeated start was NACKed on shared bus, fallback to stop condition
                Wire.beginTransmission(AS5600_ADDR);
                Wire.write(reg);
                if (Wire.endTransmission(true) != 0) return false;
            }
            if (Wire.requestFrom((uint8_t)AS5600_ADDR, (uint8_t)2) != 2) return false;
            *outValue = (((uint16_t)Wire.read() << 8) | Wire.read()) & 0x0FFF;
            _connected = true;
            return true;
        }

        case BUS_WIRE1: {
            Wire1.setClock(100000);
            Wire1.beginTransmission(AS5600_ADDR);
            Wire1.write(reg);
            if (Wire1.endTransmission(false) != 0) return false; // Repeated start
            if (Wire1.requestFrom((uint8_t)AS5600_ADDR, (uint8_t)2) != 2) return false;
            *outValue = (((uint16_t)Wire1.read() << 8) | Wire1.read()) & 0x0FFF;
            _connected = true;
            return true;
        }

        case BUS_SOFT: {
            if (!_softBus) return false;
            if (!_softBus->beginTransmission(AS5600_ADDR)) {
                _softBus->endTransmission();
                return false;
            }
            if (!_softBus->write(reg)) {
                _softBus->endTransmission();
                return false;
            }
            _softBus->endTransmission();
            if (_softBus->requestFrom(AS5600_ADDR, (uint8_t)2) < 2) return false;
            uint8_t msb = _softBus->read();
            uint8_t lsb = _softBus->read();
            // Discard floating / disconnected readings (all 1s)
            if (msb == 0xFF && lsb == 0xFF) return false;
            *outValue = (((uint16_t)msb << 8) | lsb) & 0x0FFF;
            _connected = true;
            return true;
        }
    }
    return false;
}
