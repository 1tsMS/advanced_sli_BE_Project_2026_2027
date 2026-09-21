// ============================================================
//  Load Chart — Implementation
// ============================================================
#include "load_chart.h"
#include <Preferences.h>
#include <math.h>
#include <string.h>

static const char* LC_NVS_NAMESPACE = "sli_lc";

void LoadChart::loadFromFlash() {
    Preferences prefs;
    prefs.begin(LC_NVS_NAMESPACE, true);   // read-only
    uint8_t n     = prefs.getUChar("n", 0);
    size_t  bytes = prefs.getBytesLength("e");

    if (n > 0 && n <= LC_MAX_ENTRIES && bytes == (size_t)n * sizeof(LoadChartEntry)) {
        prefs.getBytes("e", _active, bytes);
        _count = n;
    } else {
        _count = 0;   // nothing saved yet, or the record is corrupt
    }
    prefs.end();
}

LoadChartStatus LoadChart::beginUpload(uint16_t expectedCount) {
    if (expectedCount == 0 || expectedCount > LC_MAX_ENTRIES) {
        _uploading = false;
        return LC_ERR_BAD_COUNT;
    }
    _stagingCount    = 0;
    _stagingExpected = (uint8_t)expectedCount;
    _uploading       = true;
    return LC_OK;
}

LoadChartStatus LoadChart::addEntry(float angle, float extensionMM, float limitKg) {
    if (!_uploading) return LC_ERR_NOT_UPLOADING;
    if (_stagingCount >= _stagingExpected) return LC_ERR_OVERFLOW;

    if (isnan(angle) || isnan(extensionMM) || isnan(limitKg) ||
        angle < 0.0f       || angle > LC_MAX_ANGLE_DEG ||
        extensionMM < 0.0f || extensionMM > LC_MAX_EXT_MM ||
        limitKg < 0.0f     || limitKg > LC_MAX_LIMIT_KG) {
        return LC_ERR_RANGE;
    }

    _staging[_stagingCount++] = { angle, extensionMM, limitKg };
    return LC_OK;
}

LoadChartStatus LoadChart::commit() {
    if (!_uploading) return LC_ERR_NOT_UPLOADING;
    if (_stagingCount != _stagingExpected) return LC_ERR_INCOMPLETE;

    // 1. Persist first: if flash fails the active chart is left untouched
    const size_t bytes = (size_t)_stagingCount * sizeof(LoadChartEntry);
    Preferences prefs;
    if (!prefs.begin(LC_NVS_NAMESPACE, false)) return LC_ERR_FLASH;
    bool ok = (prefs.putUChar("n", _stagingCount) == sizeof(uint8_t)) &&
              (prefs.putBytes("e", _staging, bytes) == bytes);
    prefs.end();
    if (!ok) return LC_ERR_FLASH;

    // 2. Swap the active chart under the shared mutex (readers copy under it too).
    //    On timeout the staged chart is kept, so repeating LC SAVE is safe.
    if (xSemaphoreTake(sensorMutex, pdMS_TO_TICKS(100)) != pdTRUE) return LC_ERR_BUSY;
    memcpy(_active, _staging, bytes);
    _count = _stagingCount;
    xSemaphoreGive(sensorMutex);

    _uploading = false;
    return LC_OK;
}

float LoadChart::lookupLimit(float angleDeg, float extensionMM) const {
    float best = 0.0f;
    for (uint8_t i = 0; i < _count; i++) {
        const LoadChartEntry& e = _active[i];
        if (e.angle <= angleDeg + LC_ANGLE_TOL_DEG &&
            e.extensionMM >= extensionMM - LC_EXT_TOL_MM &&
            e.limitKg > best) {
            best = e.limitKg;
        }
    }
    return best;
}

bool LoadChart::get(uint8_t i, LoadChartEntry* out) const {
    if (i >= _count) return false;
    *out = _active[i];
    return true;
}

const char* LoadChart::statusName(LoadChartStatus s) {
    switch (s) {
        case LC_OK:               return "OK";
        case LC_ERR_BAD_COUNT:    return "BAD_COUNT";
        case LC_ERR_NOT_UPLOADING:return "NOT_UPLOADING";
        case LC_ERR_OVERFLOW:     return "OVERFLOW";
        case LC_ERR_RANGE:        return "RANGE";
        case LC_ERR_INCOMPLETE:   return "INCOMPLETE";
        case LC_ERR_FLASH:        return "FLASH";
        case LC_ERR_BUSY:         return "BUSY";
    }
    return "UNKNOWN";
}
