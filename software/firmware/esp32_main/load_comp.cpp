// ============================================================
//  Load Compensation — Implementation
// ============================================================
#include "load_comp.h"
#include <Preferences.h>
#include <math.h>
#include <string.h>

static const char* LG_NVS_NAMESPACE = "sli_lg";

void LoadCompensation::loadFromFlash() {
    Preferences prefs;
    prefs.begin(LG_NVS_NAMESPACE, true);   // read-only
    uint8_t n     = prefs.getUChar("n", 0);
    size_t  bytes = prefs.getBytesLength("p");

    _count = 0;
    if (n > 0 && n <= LCOMP_MAX_POINTS && bytes == (size_t)n * sizeof(GainPoint)) {
        GainPoint tmp[LCOMP_MAX_POINTS];
        prefs.getBytes("p", tmp, bytes);
        // Never trust flash blindly: a corrupt gain would silently skew every load reading
        bool ok = true;
        for (uint8_t i = 0; i < n; i++) {
            if (!(tmp[i].gain >= LCOMP_GAIN_MIN && tmp[i].gain <= LCOMP_GAIN_MAX)) ok = false;
        }
        if (ok) {
            memcpy(_pts, tmp, bytes);
            _count = n;
        }
    }
    prefs.end();
}

float LoadCompensation::gainAt(float angleDeg) const {
    if (_count == 0) return 1.0f;
    if (_count == 1 || angleDeg <= _pts[0].angle) return _pts[0].gain;
    if (angleDeg >= _pts[_count - 1].angle) return _pts[_count - 1].gain;

    for (uint8_t i = 1; i < _count; i++) {
        if (angleDeg <= _pts[i].angle) {
            const GainPoint& a = _pts[i - 1];
            const GainPoint& b = _pts[i];
            float span = b.angle - a.angle;
            if (span <= 0.0f) return b.gain;
            float f = (angleDeg - a.angle) / span;
            return a.gain + f * (b.gain - a.gain);
        }
    }
    return _pts[_count - 1].gain;
}

float LoadCompensation::correct(float measuredKg, float angleDeg) const {
    return measuredKg / gainAt(angleDeg);
}

LoadCompStatus LoadCompensation::record(float angleDeg, float measuredKg, float knownKg) {
    if (isnan(knownKg) || knownKg < LCOMP_MIN_KNOWN_KG || knownKg > LCOMP_MAX_KNOWN_KG) {
        return LCOMP_ERR_KNOWN_RANGE;
    }
    if (isnan(measuredKg) || measuredKg < LCOMP_MIN_MEASURED_KG) return LCOMP_ERR_NO_LOAD;

    float gain = measuredKg / knownKg;
    if (gain < LCOMP_GAIN_MIN || gain > LCOMP_GAIN_MAX) return LCOMP_ERR_GAIN_RANGE;

    // Work on a copy; only this (command) task ever writes the table
    GainPoint tmp[LCOMP_MAX_POINTS];
    uint8_t n = _count;
    memcpy(tmp, _pts, n * sizeof(GainPoint));

    // Replace a point recorded at (nearly) the same angle...
    int8_t existing = -1;
    for (uint8_t i = 0; i < n; i++) {
        if (fabsf(tmp[i].angle - angleDeg) <= LCOMP_MERGE_DEG) { existing = (int8_t)i; break; }
    }
    if (existing >= 0) {
        tmp[existing].angle = angleDeg;
        tmp[existing].gain  = gain;
    } else {
        // ...otherwise append (the sort below puts it in place)
        if (n >= LCOMP_MAX_POINTS) return LCOMP_ERR_FULL;
        tmp[n].angle = angleDeg;
        tmp[n].gain  = gain;
        n++;
    }

    // Keep the table sorted by angle (insertion sort; at most 8 points)
    for (uint8_t i = 1; i < n; i++) {
        for (uint8_t j = i; j > 0 && tmp[j].angle < tmp[j - 1].angle; j--) {
            GainPoint t = tmp[j]; tmp[j] = tmp[j - 1]; tmp[j - 1] = t;
        }
    }
    return commit(tmp, n);
}

LoadCompStatus LoadCompensation::clear() {
    return commit(nullptr, 0);
}

LoadCompStatus LoadCompensation::commit(const GainPoint* pts, uint8_t n) {
    // 1. Persist first: if flash fails the active table is left untouched
    Preferences prefs;
    if (!prefs.begin(LG_NVS_NAMESPACE, false)) return LCOMP_ERR_FLASH;
    bool ok = (prefs.putUChar("n", n) == sizeof(uint8_t));
    if (ok && n > 0) {
        size_t bytes = (size_t)n * sizeof(GainPoint);
        ok = (prefs.putBytes("p", pts, bytes) == bytes);
    }
    prefs.end();
    if (!ok) return LCOMP_ERR_FLASH;

    // 2. Swap under the shared mutex (SafetyTask reads under it too)
    if (xSemaphoreTake(sensorMutex, pdMS_TO_TICKS(100)) != pdTRUE) return LCOMP_ERR_BUSY;
    if (n > 0) memcpy(_pts, pts, (size_t)n * sizeof(GainPoint));
    _count = n;
    xSemaphoreGive(sensorMutex);
    return LCOMP_OK;
}

bool LoadCompensation::get(uint8_t i, GainPoint* out) const {
    if (i >= _count) return false;
    *out = _pts[i];
    return true;
}

const char* LoadCompensation::statusName(LoadCompStatus s) {
    switch (s) {
        case LCOMP_OK:              return "OK";
        case LCOMP_ERR_KNOWN_RANGE: return "KNOWN_WEIGHT_RANGE";
        case LCOMP_ERR_NO_LOAD:     return "NO_LOAD";
        case LCOMP_ERR_GAIN_RANGE:  return "GAIN_RANGE";
        case LCOMP_ERR_FULL:        return "TABLE_FULL";
        case LCOMP_ERR_FLASH:       return "FLASH";
        case LCOMP_ERR_BUSY:        return "BUSY";
    }
    return "UNKNOWN";
}
