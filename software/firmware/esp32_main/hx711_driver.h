// ============================================================
//  HX711 Load Cell Driver — Backed by standard HX711 library (bogde)
//  FreeRTOS Non-Blocking Wrapper:
//  Only reads when scale.is_ready() is true, returning cached
//  values otherwise so FreeRTOS tasks are never starved.
// ============================================================
#ifndef HX711_DRIVER_H
#define HX711_DRIVER_H

#include "config.h"
#include <HX711.h>

class HX711Driver {
public:
    HX711Driver(uint8_t dtPin, uint8_t sckPin)
        : _dt(dtPin), _sck(sckPin),
          _lastRawValue(0), _lastWeight(0.0f), _lastSampleMs(0), _connected(false) {}

    /** Initialize pins with standard HX711.begin(). */
    void begin();

    bool isConnected() const { return _connected; }

    /**
     * Non-blocking calibrated weight read in kg.
     * Applies tare offset and scale calibration factor.
     */
    float getWeight();

    /**
     * Tare the load cell to set zero offset.
     * Blocks briefly for averaging (call only during calibration, not in hot loop).
     */
    void tare(uint8_t samples = 10);

    /** True if a new sample arrived within maxAgeMs (false if the cell is unplugged or stuck). */
    bool isFresh(uint32_t maxAgeMs) const { return _connected && (millis() - _lastSampleMs) <= maxAgeMs; }

    /** Get the last read raw 24-bit ADC counts. */
    int32_t getLastRaw() const { return _lastRawValue; }

private:
    uint8_t _dt, _sck;
    HX711   _scale;
    int32_t _lastRawValue;
    float   _lastWeight;
    uint32_t _lastSampleMs;
    bool    _connected;
};

#endif // HX711_DRIVER_H
