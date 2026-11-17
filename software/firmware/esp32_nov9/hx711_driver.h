// ============================================================
//  HX711 Load Cell Driver — backed by the RobTillaart HX711 library
//
//  Non-blocking wrapper for FreeRTOS: a conversion is only clocked out
//  when DOUT already says it is ready, and exactly ONE conversion is read
//  per sample. (The library's read() busy-waits for DOUT, so calling it a
//  second time right after a read, e.g. via get_units(), stalls the
//  caller until the next conversion: ~100 ms at the HX711's 10 SPS.)
//
//  Only SensorTask may call getWeight()/tare steps: the HX711 is
//  bit-banged, and two cores clocking it at once corrupt the reading.
// ============================================================
#ifndef HX711_DRIVER_H
#define HX711_DRIVER_H

#include "config.h"
#include "loadcell_health.h"
#include <HX711.h>

class HX711Driver {
public:
    HX711Driver(uint8_t dtPin, uint8_t sckPin) : _dt(dtPin), _sck(sckPin) {}

    /** Initialise pins, apply the default scale and tare (blocking, setup() only). */
    void begin();

    bool isConnected() const { return _connected; }

    /**
     * Non-blocking: if a new conversion is ready, read it and update the
     * cached weight; otherwise return the cached weight.
     * @return weight in kg (tare and scale applied, clamped at 0)
     */
    float getWeight();

    /** Raw 24-bit counts of the last conversion (for load-cell calibration). */
    int32_t getLastRaw() const { return _lastRawValue; }

    /** Is the BRIDGE alive (not just the HX711)? Broken wire = SATURATED / FROZEN / NEGATIVE. */
    LoadCellHealth health() const { return _monitor.health(); }
    /** Raw-count noise (max - min) over the last ~3 s: tens of counts on a live cell. */
    int32_t noiseSpan() const { return _monitor.lastSpan(); }

    /** True once at least one conversion arrived in the last `maxAgeMs`. */
    bool isFresh(uint32_t maxAgeMs) const { return _lastSampleMs != 0 && (millis() - _lastSampleMs) <= maxAgeMs; }

    // ---- Non-blocking tare: start, then feed() is called by getWeight() ----
    /** Average the next `samples` conversions and use them as the new zero. */
    void startTare(uint8_t samples = 10);
    bool tareBusy() const { return _tareTarget > 0; }
    /** True exactly once after a started tare finished. */
    bool takeTareDone();

    // ---- Non-blocking average for calibration points ----
    /** Average the next `samples` conversions (tare offset removed). */
    void startAverage(uint8_t samples);
    bool averageBusy() const { return _avgTarget > 0; }
    /** True exactly once when a started average finished; *counts = mean(raw - offset). */
    bool takeAverageDone(double* counts);

    /** Calibration factor in raw counts per kg (sign included, so a reversed cell works). */
    float getScale() { return _scale.get_scale(); }
    void  setScale(float countsPerKg) { if (countsPerKg != 0.0f) _scale.set_scale(countsPerKg); }

    /** Current tare offset (for NVS persistence). */
    int32_t getOffset() { return (int32_t)_scale.get_offset(); }
    /** Restore a previously saved tare offset (skips re-reading the HX711). */
    void setOffset(int32_t off) { _scale.set_offset(off); }   // presence is decided by begin()/reads, not by this

private:
    uint8_t  _dt, _sck;
    HX711    _scale;
    int32_t  _lastRawValue = 0;
    float    _lastWeight   = 0.0f;
    bool     _connected    = false;
    uint32_t _lastSampleMs = 0;

    uint8_t  _tareTarget = 0;   // samples wanted (0 = no tare running)
    uint8_t  _tareCount  = 0;
    double   _tareSum    = 0.0;
    bool     _tareDone   = false;

    uint8_t  _avgTarget = 0;
    uint8_t  _avgCount  = 0;
    double   _avgSum    = 0.0;
    double   _avgResult = 0.0;
    bool     _avgDone   = false;

    LoadCellMonitor _monitor;
};

#endif // HX711_DRIVER_H
