// ============================================================
//  HX711 Driver — Implementation backed by bogde/HX711
// ============================================================
#include "hx711_driver.h"

void HX711Driver::begin() {
    _scale.begin(_dt, _sck);
    _scale.set_scale(HX711_DEFAULT_SCALE);

    // Give HX711 chip time to settle on power up
    delay(500);
    _connected = _scale.is_ready();

    if (_connected) {
        // Tare 20 samples for stable zero offset (matches loadcell_approx.ino)
        _scale.tare(20);
    }
}

float HX711Driver::getWeight() {
    if (_scale.is_ready()) {
        _lastRawValue = _scale.read();  // Read raw count directly
        float w = _scale.get_units(1);

        // Deadband: eliminate sensor noise near zero (matches loadcell_approx.ino)
        if (w > -0.02f && w < 0.02f) {
            w = 0.0f;
        }
        if (w < 0.0f) {
            w = 0.0f;
        }

        _lastWeight = w;
        _lastSampleMs = millis();
        _connected = true;
    }
    return _lastWeight;
}

void HX711Driver::tare(uint8_t samples) {
    // Wait for sensor ready with a short 500ms safety timeout
    uint32_t timeout = millis() + 500;
    while (!_scale.is_ready() && millis() < timeout) {
        delay(5);
    }

    if (_scale.is_ready()) {
        _scale.tare(samples);
        _connected = true;
    }
}
