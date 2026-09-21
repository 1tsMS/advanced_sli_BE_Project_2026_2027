// ============================================================
//  FSR (Force Sensitive Resistor) Analog Reader
//  Reads 4 outrigger force sensors via ESP32 ADC1 pins.
//  ADC1 pins (GPIO 32-39) are safe to use with WiFi enabled.
// ============================================================
#ifndef FSR_READER_H
#define FSR_READER_H

#include "config.h"

/**
 * Reads 4 FSR sensors connected to ADC1 input-only pins.
 * Returns raw ADC values (0–4095 on ESP32's 12-bit ADC).
 *
 * The FSR sensors sit under each outrigger leg to measure
 * ground reaction force distribution. Uneven readings indicate
 * the crane is tilting toward one side.
 */
class FSRReader {
public:
    FSRReader() {
        _pins[0] = FSR1_PIN;
        _pins[1] = FSR2_PIN;
        _pins[2] = FSR3_PIN;
        _pins[3] = FSR4_PIN;
    }

    /** Configure ADC resolution. Call in setup(). */
    void begin() {
        analogReadResolution(12);   // 12-bit → 0–4095
        // ADC1 pins are input-only, no pinMode needed
    }

    /** Read all 4 FSR values into the provided array. */
    void readAll(uint16_t values[4]) {
        for (uint8_t i = 0; i < 4; i++) {
            values[i] = (uint16_t)analogRead(_pins[i]);
        }
    }

private:
    uint8_t _pins[4];
};

#endif // FSR_READER_H
