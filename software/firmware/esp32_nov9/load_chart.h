// ============================================================
//  Load chart: rated capacity vs boom angle x extension, per swing zone
//
//  Pure logic (no Arduino / RTOS), unit-tested on the PC in host_tests/.
//
//  Like a real crane chart: rows are boom angles, columns are telescope
//  extensions, each cell is the rated payload in kg. Between cells the
//  capacity is bilinearly interpolated. Swing zones (e.g. over the rear)
//  scale the whole chart by a factor, the way real cranes de-rate over the
//  side or rear where the outrigger base is narrower.
//
//  Outside the chart (beyond a small tolerance) the capacity is 0: a real
//  load moment indicator does not allow operation where the chart has no
//  value, so the chart edges also act as soft travel limits.
//
//  Uploaded from the PC one line at a time (fits the 128-char command line):
//    LC BEGIN <nAngles> <nExt> <name>
//    LC A <deg> <deg> ...           boom angles, strictly increasing
//    LC E <mm> <mm> ...             extensions, strictly increasing
//    LC R <row> <kg> <kg> ...       capacities for angle row <row>
//    LC Z <fromDeg> <toDeg> <factor>   optional swing zone (up to 4, may wrap 360)
//    LC P <warnPct> <cutPct>        optional thresholds (default 90 / 100)
//    LC COMMIT                      validate, activate, save to flash
//    LC GET                         print the active chart
// ============================================================
#ifndef LOAD_CHART_H
#define LOAD_CHART_H

#include <stdint.h>
#include <stddef.h>

#define LC_MAX_ANGLES   8
#define LC_MAX_EXTS     6
#define LC_MAX_ZONES    4
#define LC_NAME_LEN     16
#define LC_VERSION      1      // bump when the struct layout changes (flash copy is then ignored)

#define LC_TOL_DEG      1.0f   // angle beyond the first/last row still clamped to it
#define LC_TOL_MM       2.0f   // extension beyond the first/last column still clamped
#define LC_MAX_CAP_KG   10.0f  // any cell above this is a typo, not a capacity

struct LoadZone {
    float fromDeg, toDeg;      // swing range [from, to), wraps through 360 when from > to
    float factor;              // capacity multiplier in this zone (0..1.5)
};

struct LoadChart {
    uint16_t version;
    uint8_t  nA, nE, nZ;
    float    angDeg[LC_MAX_ANGLES];
    float    extMm[LC_MAX_EXTS];
    float    capKg[LC_MAX_ANGLES][LC_MAX_EXTS];
    LoadZone zones[LC_MAX_ZONES];
    float    warnPct, cutPct;
    char     name[LC_NAME_LEN];
};

/** A chart being uploaded line by line; only COMMIT turns it into the active one. */
struct LoadChartStage {
    LoadChart chart;
    bool      begun;
    bool      gotA, gotE;
    uint8_t   rowMask;         // bit i = row i received
};

enum LcParse : uint8_t {
    LC_PARSE_OK,               // line applied to the stage
    LC_PARSE_COMMIT,           // caller: lc_stageComplete + lc_validate, then activate
    LC_PARSE_GET,              // caller: print the active chart
    LC_PARSE_ERR
};

/** Apply one "LC ..." line (text after "LC"). On error *why names the problem. */
LcParse lc_parse(LoadChartStage* st, const char* args, const char** why);

/** Every part announced by BEGIN has arrived. */
bool lc_stageComplete(const LoadChartStage& st, const char** why);

/** Structural and range checks. A chart that fails is never used. */
bool lc_validate(const LoadChart& c, const char** why);

/** Zone factor for a swing angle (any range of degrees). zone = index or -1 (none, factor 1). */
float lc_zoneFactor(const LoadChart& c, float swingDeg, int8_t* zone);

/** Rated capacity in kg at this pose; 0 outside the chart. */
float lc_capacity(const LoadChart& c, float boomDeg, float extMm, float swingDeg, int8_t* zone);

/** How far outside the chart (beyond the tolerance band) a pose is: degrees + mm, 0 inside. */
float lc_outsideBy(const LoadChart& c, float boomDeg, float extMm);

#endif // LOAD_CHART_H
