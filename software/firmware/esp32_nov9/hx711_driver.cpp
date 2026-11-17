// ============================================================
//  HX711 Driver — Implementation (RobTillaart HX711 library)
// ============================================================
#include "hx711_driver.h"

void HX711Driver::begin() {
    // fastProcessor = true: adds the 1 us clock delays the HX711 needs on a
    // 240 MHz ESP32 (clock-high time must be >= 0.2 us).
    _scale.begin(_dt, _sck, true);
    _scale.set_scale(HX711_DEFAULT_SCALE);

    // Wait up to 1 s for the HX711's DOUT to go LOW (it powers up async).
    // The old delay(500) + is_ready() missed it roughly half the time.
    if (!_scale.wait_ready_timeout(1000)) {
        _connected = false;
        return;
    }

    // Discard the first 3 conversions: the HX711 datasheet says the analog
    // front-end settles in ~400 ms, and the early reads are noisy.
    for (uint8_t i = 0; i < 3; i++) {
        if (_scale.wait_ready_timeout(200)) _scale.read();
    }

    _connected    = true;
    _lastSampleMs = millis();
    // Do NOT auto-tare: a load on the hook at power-up would give a wrong
    // zero. sensor_task will either restore the saved NVS offset or the
    // user will send CAL0 explicitly.
}

float HX711Driver::getWeight() {
    if (!_scale.is_ready()) return _lastWeight;   // conversion not finished: cached value

    // DOUT is already low, so this read() does not wait
    const int32_t raw = (int32_t)_scale.read();
    _lastRawValue = raw;
    _lastSampleMs = millis();
    _connected    = true;

    if (_tareTarget > 0) {
        _tareSum += raw;
        if (++_tareCount >= _tareTarget) {
            _scale.set_offset((int32_t)(_tareSum / _tareCount));
            _tareTarget = 0;
            _tareDone   = true;
        }
    }

    if (_avgTarget > 0) {
        _avgSum += (double)(raw - _scale.get_offset());
        if (++_avgCount >= _avgTarget) {
            _avgResult = _avgSum / _avgCount;
            _avgTarget = 0;
            _avgDone   = true;
        }
    }

    // Same maths as the library's get_units(), without a second read
    float w = (float)(raw - _scale.get_offset()) / _scale.get_scale();

    // Bridge health. The NEGATIVE check needs a tare: before one, raw - 0 can be
    // any value, so it only runs once an offset exists.
    _monitor.push(raw, _scale.get_offset() != 0 ? w : 0.0f, (int32_t)_scale.get_offset());

    // Deadband: eliminate sensor noise near zero (matches loadcell_approx.ino)
    if (w > -0.02f && w < 0.02f) w = 0.0f;
    if (w < 0.0f) w = 0.0f;

    _lastWeight = w;
    return w;
}

void HX711Driver::startTare(uint8_t samples) {
    _tareSum    = 0.0;
    _tareCount  = 0;
    _tareDone   = false;
    _tareTarget = samples > 0 ? samples : 1;
}

void HX711Driver::startAverage(uint8_t samples) {
    _avgSum    = 0.0;
    _avgCount  = 0;
    _avgDone   = false;
    _avgTarget = samples > 0 ? samples : 1;
}

bool HX711Driver::takeAverageDone(double* counts) {
    if (!_avgDone) return false;
    _avgDone = false;
    *counts  = _avgResult;
    return true;
}

bool HX711Driver::takeTareDone() {
    if (!_tareDone) return false;
    _tareDone = false;
    return true;
}
