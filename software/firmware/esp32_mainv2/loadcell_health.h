// ============================================================
//  Load cell health: is the BRIDGE alive, not just the HX711?
//
//  Pure logic (no Arduino), unit-tested on the PC (host_tests/test_loadcell_health.cpp).
//  The HX711 keeps converting when a load-cell wire breaks, so "conversions
//  arrive" (HX711Driver::isFresh) says nothing about the cell. A broken wire
//  shows up as one of:
//    SATURATED  a signal wire (S+ / S-) open: the input floats to a rail and the
//               reading pins at the 24-bit limit (+-8 388 607)
//    FROZEN     the reading doesn't move at all: a live bridge always has some
//               counts of noise (tens of counts at gain 128), a dead one doesn't
//    NEGATIVE   far below the tare zero: nothing on a hook pushes UP, so a large
//               negative load means the bridge lost its excitation (E+ / E-)
//               or the cell came loose
//    OPEN       the reading sits near 0 counts while the tare says this cell's zero
//               is far from 0: the cell is unplugged. Measured on the crane with all
//               4 wires out: raw 2161..2225 (the open inputs float together), while a
//               real bridge has its own offset. Needs |tare offset| > OPEN_MIN_OFFSET,
//               so a cell whose natural zero happens to be near 0 can't use this check.
//  Each needs to persist (a few samples / ~1 s) before it counts, so a single
//  odd conversion never trips the SLI.
// ============================================================
#ifndef LOADCELL_HEALTH_H
#define LOADCELL_HEALTH_H

#include <stdint.h>

enum LoadCellHealth : uint8_t {
    LC_HEALTH_OK = 0,
    LC_HEALTH_SATURATED,
    LC_HEALTH_FROZEN,
    LC_HEALTH_NEGATIVE,
    LC_HEALTH_OPEN,
};

inline const char* loadCellHealthName(LoadCellHealth h) {
    switch (h) {
        case LC_HEALTH_SATURATED: return "SATURATED";
        case LC_HEALTH_FROZEN:    return "FROZEN";
        case LC_HEALTH_NEGATIVE:  return "NEGATIVE";
        case LC_HEALTH_OPEN:      return "OPEN";
        default:                  return "OK";
    }
}

class LoadCellMonitor {
public:
    static constexpr int32_t RAIL         = 8388000;  // |raw| at or above = at the 24-bit rail
    static constexpr uint8_t RAIL_SAMPLES = 3;        // consecutive rail samples -> SATURATED
    static constexpr uint8_t FROZEN_WIN   = 30;       // window for the noise check (3 s at 10 SPS)
    static constexpr int32_t FROZEN_SPAN  = 2;        // max - min within the window <= this -> FROZEN
    static constexpr float   NEG_KG       = -0.5f;    // below this (tare removed) ...
    static constexpr uint8_t NEG_SAMPLES  = 10;       // ... for this many samples -> NEGATIVE
    static constexpr int32_t OPEN_BAND       = 10000; // |raw| below this = "near 0 counts"
    static constexpr int32_t OPEN_MIN_OFFSET = 40000; // only when the tare zero is at least this far from 0
    static constexpr uint8_t OPEN_SAMPLES    = 10;    // ~1 s at 10 SPS

    /** One conversion. kg = (raw - tare offset) / scale, before any clamping; offset = tare (0 = none). */
    void push(int32_t raw, float kg, int32_t offset = 0) {
        _rail = (raw >= RAIL || raw <= -RAIL) ? (uint8_t)(_rail < 255 ? _rail + 1 : 255) : 0;
        _neg  = (kg < NEG_KG) ? (uint8_t)(_neg < 255 ? _neg + 1 : 255) : 0;
        const bool openNow = (raw > -OPEN_BAND && raw < OPEN_BAND) &&
                             (offset >= OPEN_MIN_OFFSET || offset <= -OPEN_MIN_OFFSET);
        _open = openNow ? (uint8_t)(_open < 255 ? _open + 1 : 255) : 0;

        if (_n == 0) { _min = _max = raw; }
        if (raw < _min) _min = raw;
        if (raw > _max) _max = raw;
        if (++_n >= FROZEN_WIN) {             // window full: judge it, start the next one
            _frozen = (_max - _min) <= FROZEN_SPAN;
            _lastSpan = _max - _min;
            _n = 0;
        }
    }

    LoadCellHealth health() const {
        if (_rail >= RAIL_SAMPLES) return LC_HEALTH_SATURATED;
        if (_open >= OPEN_SAMPLES) return LC_HEALTH_OPEN;
        if (_frozen)               return LC_HEALTH_FROZEN;
        if (_neg >= NEG_SAMPLES)   return LC_HEALTH_NEGATIVE;
        return LC_HEALTH_OK;
    }

    /** max - min of the raw counts over the last full window (noise level, for DBG). */
    int32_t lastSpan() const { return _lastSpan; }

private:
    uint8_t _rail = 0, _neg = 0, _open = 0, _n = 0;
    int32_t _min = 0, _max = 0, _lastSpan = -1;
    bool    _frozen = false;
};

#endif // LOADCELL_HEALTH_H
