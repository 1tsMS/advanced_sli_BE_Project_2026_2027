// ============================================================
//  Load Chart — rated-capacity table stored in ESP32 flash (NVS)
//
//  Each entry says: at boom angle A and telescope extension E, the
//  safe working load is L kg. The chart is uploaded from the dashboard:
//
//    LC UPLOAD <n>       start an upload of n entries   -> $LC,READY,<n>
//    LC <a>,<e>,<l>      one entry (sent n times)       (errors -> $LC,ERR,<why>)
//    LC SAVE             validate + write to flash      -> $LC,OK,<n>
//    LC GET              read the stored chart back     -> $LC,BEGIN,<n>
//                                                          $LC,E,<i>,<a>,<e>,<l> ...
//                                                          $LC,END
//
//  Entries are staged in RAM and only replace the active chart when
//  LC SAVE succeeds, so an interrupted upload never leaves a half chart.
//
//  Thread safety: the active chart is swapped under sensorMutex. Any
//  reader (e.g. a future SafetyTask) must copy entries under sensorMutex.
// ============================================================
#ifndef LOAD_CHART_H
#define LOAD_CHART_H

#include "config.h"

#define LC_MAX_ENTRIES  32

// Edge tolerance for the limit lookup, so sensor noise at the very edge of the
// chart (e.g. a retracted telescope reading +0.3 mm) does not fall "outside" it.
#define LC_ANGLE_TOL_DEG  0.5f
#define LC_EXT_TOL_MM     5.0f

// Sanity limits (the backend enforces the same numbers)
#define LC_MAX_ANGLE_DEG  90.0f
#define LC_MAX_EXT_MM     2000.0f
#define LC_MAX_LIMIT_KG   100.0f

struct LoadChartEntry {
    float angle;        // boom angle, degrees from horizontal
    float extensionMM;  // telescope extension, mm
    float limitKg;      // safe working load at that angle/extension
};

enum LoadChartStatus : uint8_t {
    LC_OK = 0,
    LC_ERR_BAD_COUNT,      // UPLOAD count is 0 or above LC_MAX_ENTRIES
    LC_ERR_NOT_UPLOADING,  // entry/SAVE without a preceding UPLOAD
    LC_ERR_OVERFLOW,       // more entries than announced
    LC_ERR_RANGE,          // value NaN or outside the sanity limits
    LC_ERR_INCOMPLETE,     // SAVE before all announced entries arrived
    LC_ERR_FLASH,          // NVS write failed
    LC_ERR_BUSY            // could not take sensorMutex to swap the chart
};

class LoadChart {
public:
    LoadChart() : _count(0), _stagingCount(0), _stagingExpected(0), _uploading(false) {}

    /** Restore the saved chart from NVS. Call once in setup(). */
    void loadFromFlash();

    LoadChartStatus beginUpload(uint16_t expectedCount);
    LoadChartStatus addEntry(float angle, float extensionMM, float limitKg);
    LoadChartStatus commit();

    uint8_t count() const { return _count; }

    /** True once a chart has been saved (otherwise SAFE_LOAD_DEFAULT_KG applies). */
    bool hasChart() const { return _count > 0; }

    /**
     * Safe working load at this boom angle and extension.
     *
     * Conservative rule: a chart row is "never safer" than the current position if
     * its angle is lower-or-equal and its extension greater-or-equal. The limit is
     * the highest limit among such rows; 0 kg when no row qualifies (outside the
     * chart). A small tolerance (LC_ANGLE_TOL_DEG / LC_EXT_TOL_MM) applies at the edges.
     *
     * Caller must hold sensorMutex (LC SAVE can swap the chart).
     */
    float lookupLimit(float angleDeg, float extensionMM) const;

    /** Copy entry i of the active chart. Caller should hold sensorMutex. */
    bool get(uint8_t i, LoadChartEntry* out) const;

    /** Short text for a status code, used in $LC,ERR,<name> replies. */
    static const char* statusName(LoadChartStatus s);

private:
    LoadChartEntry _active[LC_MAX_ENTRIES];
    uint8_t        _count;

    LoadChartEntry _staging[LC_MAX_ENTRIES];
    uint8_t        _stagingCount;
    uint8_t        _stagingExpected;
    bool           _uploading;
};

#endif // LOAD_CHART_H
