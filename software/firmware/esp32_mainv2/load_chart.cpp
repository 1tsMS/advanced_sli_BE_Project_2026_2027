// ============================================================
//  Load chart — Implementation (pure logic)
// ============================================================
#include "load_chart.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

// ---------------------------------------------------------------- parsing

static const char* skipSpaces(const char* p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/** "WORD" at p (followed by a space or end)? Advances p past it. */
static bool takeWord(const char** p, const char* word) {
    const size_t n = strlen(word);
    if (strncmp(*p, word, n) != 0) return false;
    const char next = (*p)[n];
    if (next != '\0' && next != ' ' && next != '\t') return false;
    *p = skipSpaces(*p + n);
    return true;
}

/** Exactly n numbers and nothing after them. */
static bool readFloats(const char* p, float* out, int n) {
    for (int i = 0; i < n; i++) {
        char* end;
        const float v = strtof(p, &end);
        if (end == p || !isfinite(v)) return false;
        out[i] = v;
        p = end;
    }
    return *skipSpaces(p) == '\0';
}

LcParse lc_parse(LoadChartStage* st, const char* args, const char** why) {
    const char* p = skipSpaces(args);
    *why = "";

    if (takeWord(&p, "BEGIN")) {
        char* end;
        const long nA = strtol(p, &end, 10);
        if (end == p) { *why = "BEGIN_NA"; return LC_PARSE_ERR; }
        p = end;
        const long nE = strtol(p, &end, 10);
        if (end == p) { *why = "BEGIN_NE"; return LC_PARSE_ERR; }
        p = skipSpaces(end);
        if (nA < 2 || nA > LC_MAX_ANGLES || nE < 2 || nE > LC_MAX_EXTS) { *why = "SIZE"; return LC_PARSE_ERR; }

        memset(st, 0, sizeof(*st));
        st->chart.version = LC_VERSION;
        st->chart.nA      = (uint8_t)nA;
        st->chart.nE      = (uint8_t)nE;
        st->chart.warnPct = 90.0f;
        st->chart.cutPct  = 100.0f;
        strncpy(st->chart.name, *p ? p : "UNNAMED", LC_NAME_LEN - 1);
        st->begun = true;
        return LC_PARSE_OK;
    }
    if (takeWord(&p, "GET"))    return LC_PARSE_GET;
    if (!st->begun) { *why = "NO_BEGIN"; return LC_PARSE_ERR; }
    if (takeWord(&p, "COMMIT")) return LC_PARSE_COMMIT;

    LoadChart& c = st->chart;
    if (takeWord(&p, "A")) {
        if (!readFloats(p, c.angDeg, c.nA)) { *why = "A_COUNT"; return LC_PARSE_ERR; }
        st->gotA = true;
        return LC_PARSE_OK;
    }
    if (takeWord(&p, "E")) {
        if (!readFloats(p, c.extMm, c.nE)) { *why = "E_COUNT"; return LC_PARSE_ERR; }
        st->gotE = true;
        return LC_PARSE_OK;
    }
    if (takeWord(&p, "R")) {
        char* end;
        const long row = strtol(p, &end, 10);
        if (end == p || row < 0 || row >= c.nA) { *why = "R_INDEX"; return LC_PARSE_ERR; }
        if (!readFloats(end, c.capKg[row], c.nE)) { *why = "R_COUNT"; return LC_PARSE_ERR; }
        st->rowMask |= (uint8_t)(1u << row);
        return LC_PARSE_OK;
    }
    if (takeWord(&p, "Z")) {
        if (c.nZ >= LC_MAX_ZONES) { *why = "Z_FULL"; return LC_PARSE_ERR; }
        float v[3];
        if (!readFloats(p, v, 3)) { *why = "Z_COUNT"; return LC_PARSE_ERR; }
        c.zones[c.nZ].fromDeg = v[0];
        c.zones[c.nZ].toDeg   = v[1];
        c.zones[c.nZ].factor  = v[2];
        c.nZ++;
        return LC_PARSE_OK;
    }
    if (takeWord(&p, "P")) {
        float v[2];
        if (!readFloats(p, v, 2)) { *why = "P_COUNT"; return LC_PARSE_ERR; }
        c.warnPct = v[0];
        c.cutPct  = v[1];
        return LC_PARSE_OK;
    }
    *why = "UNKNOWN";
    return LC_PARSE_ERR;
}

bool lc_stageComplete(const LoadChartStage& st, const char** why) {
    if (!st.begun) { *why = "NO_BEGIN";   return false; }
    if (!st.gotA)  { *why = "MISSING_A";  return false; }
    if (!st.gotE)  { *why = "MISSING_E";  return false; }
    const uint8_t all = (uint8_t)((1u << st.chart.nA) - 1u);
    if ((st.rowMask & all) != all) { *why = "MISSING_ROWS"; return false; }
    return true;
}

// ---------------------------------------------------------------- checks

static bool increasing(const float* v, uint8_t n) {
    for (uint8_t i = 0; i + 1 < n; i++) if (!(v[i + 1] > v[i])) return false;
    return true;
}

bool lc_validate(const LoadChart& c, const char** why) {
    if (c.version != LC_VERSION)                         { *why = "VERSION";    return false; }
    if (c.nA < 2 || c.nA > LC_MAX_ANGLES ||
        c.nE < 2 || c.nE > LC_MAX_EXTS || c.nZ > LC_MAX_ZONES) { *why = "SIZE"; return false; }
    if (!increasing(c.angDeg, c.nA))                     { *why = "A_ORDER";    return false; }
    if (!increasing(c.extMm,  c.nE))                     { *why = "E_ORDER";    return false; }
    if (c.angDeg[0] < -90.0f || c.angDeg[c.nA - 1] > 90.0f) { *why = "A_RANGE"; return false; }
    for (uint8_t i = 0; i < c.nA; i++)
        for (uint8_t j = 0; j < c.nE; j++)
            // > 0: a zero cell would read as "outside the chart" in the middle of it
            if (!(c.capKg[i][j] > 0.0f && c.capKg[i][j] <= LC_MAX_CAP_KG)) { *why = "CAP_RANGE"; return false; }
    for (uint8_t z = 0; z < c.nZ; z++) {
        const LoadZone& zn = c.zones[z];
        if (!(zn.fromDeg >= 0.0f && zn.fromDeg < 360.0f && zn.toDeg >= 0.0f && zn.toDeg <= 360.0f) ||
            zn.fromDeg == zn.toDeg)                      { *why = "Z_RANGE";    return false; }
        if (!(zn.factor > 0.0f && zn.factor <= 1.5f))    { *why = "Z_FACTOR";   return false; }
    }
    if (!(c.cutPct > 0.0f && c.cutPct <= 150.0f))        { *why = "CUT_PCT";    return false; }
    if (!(c.warnPct > 0.0f && c.warnPct < c.cutPct))     { *why = "WARN_PCT";   return false; }
    if (memchr(c.name, '\0', LC_NAME_LEN) == NULL)       { *why = "NAME";       return false; }
    return true;
}

// ---------------------------------------------------------------- lookup

/** Segment i0 and fraction t of x within breakpoints b; false when outside by more than tol. */
static bool locate(const float* b, uint8_t n, float x, float tol, uint8_t* i0, float* t) {
    if (!(x >= b[0] - tol && x <= b[n - 1] + tol)) return false;   // also rejects NaN
    if (x <= b[0])     { *i0 = 0;                 *t = 0.0f; return true; }
    if (x >= b[n - 1]) { *i0 = (uint8_t)(n - 2);  *t = 1.0f; return true; }
    for (uint8_t i = 0; i + 1 < n; i++) {
        if (x <= b[i + 1]) {
            *i0 = i;
            *t  = (x - b[i]) / (b[i + 1] - b[i]);
            return true;
        }
    }
    return false;
}

float lc_zoneFactor(const LoadChart& c, float swingDeg, int8_t* zone) {
    float s = fmodf(swingDeg, 360.0f);
    if (s < 0.0f) s += 360.0f;
    for (uint8_t z = 0; z < c.nZ; z++) {
        const LoadZone& zn = c.zones[z];
        const bool in = (zn.fromDeg < zn.toDeg) ? (s >= zn.fromDeg && s < zn.toDeg)
                                                : (s >= zn.fromDeg || s < zn.toDeg);   // wraps 360
        if (in) { if (zone) *zone = (int8_t)z; return zn.factor; }
    }
    if (zone) *zone = -1;
    return 1.0f;
}

float lc_capacity(const LoadChart& c, float boomDeg, float extMm, float swingDeg, int8_t* zone) {
    const float factor = lc_zoneFactor(c, swingDeg, zone);
    uint8_t ia, ie;
    float   ta, te;
    if (!locate(c.angDeg, c.nA, boomDeg, LC_TOL_DEG, &ia, &ta)) return 0.0f;
    if (!locate(c.extMm,  c.nE, extMm,   LC_TOL_MM,  &ie, &te)) return 0.0f;
    const float c00 = c.capKg[ia][ie],     c01 = c.capKg[ia][ie + 1];
    const float c10 = c.capKg[ia + 1][ie], c11 = c.capKg[ia + 1][ie + 1];
    const float lo  = c00 + (c01 - c00) * te;      // along extension at the lower angle row
    const float hi  = c10 + (c11 - c10) * te;      // ... at the upper angle row
    return (lo + (hi - lo) * ta) * factor;
}

static float excess(float x, float lo, float hi, float tol) {
    if (x < lo - tol) return (lo - tol) - x;
    if (x > hi + tol) return x - (hi + tol);
    return 0.0f;
}

float lc_outsideBy(const LoadChart& c, float boomDeg, float extMm) {
    return excess(boomDeg, c.angDeg[0], c.angDeg[c.nA - 1], LC_TOL_DEG) +
           excess(extMm,   c.extMm[0],  c.extMm[c.nE - 1],  LC_TOL_MM);
}
