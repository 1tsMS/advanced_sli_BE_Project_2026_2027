// ============================================================
//  FSR outrigger pads: 4 ADC1 inputs (input-only pins 34/35/39/36)
//
//  Each FSR is the top half of a divider with a fixed resistor to GND, so the
//  ADC reading rises with force: 0 counts = no force, 4095 counts = 3.3 V at the
//  pin (FSR resistance ~0, far beyond what the crane puts on a pad). The backend
//  turns the four readings into a centre of pressure (core/tipping.py).
//
//  Smoothing, per channel every SensorTask cycle (10 ms):
//    1. FSR_OVERSAMPLE reads averaged (ADC noise is ~+-30 counts per read)
//    2. exponential average, time constant FSR_SMOOTH_TAU_S
//  The first reading seeds the average, so it starts at the real value.
// ============================================================
#ifndef FSR_READER_H
#define FSR_READER_H

#include "config.h"

class FSRReader {
public:
    /** Configure ADC resolution. Call in setup(). */
    void begin() {
        analogReadResolution(12);   // 0-4095; ADC1 pins are input-only, no pinMode needed
    }

    /**
     * Read and smooth all four pads.
     * @param values smoothed ADC counts (0-4095)
     * @param dtS    time since the last call (s)
     */
    void readAll(uint16_t values[4], float dtS) {
        const float k = dtS / (FSR_SMOOTH_TAU_S + dtS);
        for (uint8_t i = 0; i < 4; i++) {
            uint32_t sum = 0;
            for (uint8_t n = 0; n < FSR_OVERSAMPLE; n++) sum += analogRead(_pins[i]);
            const float x = (float)sum / FSR_OVERSAMPLE;
            _avg[i] = _seeded ? _avg[i] + k * (x - _avg[i]) : x;
            values[i] = (uint16_t)(_avg[i] + 0.5f);
        }
        _seeded = true;
    }

private:
    const uint8_t _pins[4] = { FSR1_PIN, FSR2_PIN, FSR3_PIN, FSR4_PIN };
    float _avg[4]  = {0, 0, 0, 0};
    bool  _seeded  = false;
};

#endif // FSR_READER_H
