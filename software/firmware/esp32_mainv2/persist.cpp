// ============================================================
//  Persistent settings — Implementation
// ============================================================
#include "persist.h"
#include "rtos_config.h"
#include <Preferences.h>

enum : uint32_t {
    D_POS0 = 1u << 0, D_POS1 = 1u << 1, D_POS2 = 1u << 2, D_POS3 = 1u << 3,
    D_TELE = 1u << 4, D_ROPE = 1u << 5, D_IMU  = 1u << 6,
    D_ROT0 = 1u << 7, D_ROT1 = 1u << 8, D_TARE = 1u << 9,
    D_LOADSCALE = 1u << 10, D_SCALE0 = 1u << 11, D_SCALE1 = 1u << 12,
    D_CHART = 1u << 13,
};
static const uint32_t D_LAST = D_CHART;

struct Staged {
    PersistPos    pos[4];
    float         teleScale;
    bool          teleInv;
    float         ropeScale;
    bool          ropeInv;
    float         ropeExtRef;
    PersistImu    imu;
    PersistRotary rot[2];
    bool          boomCal;
    int32_t       tare;
    float         loadScale;
    float         axisScale[2];
    LoadChart     chart;
};

static Staged       _staged;
static uint32_t     _dirty = 0;
static portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t _writes = 0, _failures = 0;

static void markDirty(uint32_t bits) {
    // The caller already copied its data under _mux; wake the writer
    if (g_storageTask) xTaskNotifyGive(g_storageTask);
    (void)bits;
}

void persist_setPosition(uint8_t slot, const PersistPos& p) {
    if (slot > 3) return;
    taskENTER_CRITICAL(&_mux);
    _staged.pos[slot] = p;
    _dirty |= (D_POS0 << slot);
    taskEXIT_CRITICAL(&_mux);
    markDirty(D_POS0 << slot);
}

void persist_setTele(float scale, bool inverted) {
    taskENTER_CRITICAL(&_mux);
    _staged.teleScale = scale;
    _staged.teleInv   = inverted;
    _dirty |= D_TELE;
    taskEXIT_CRITICAL(&_mux);
    markDirty(D_TELE);
}

void persist_setRope(float scale, bool inverted, float extRef) {
    taskENTER_CRITICAL(&_mux);
    _staged.ropeScale  = scale;
    _staged.ropeInv    = inverted;
    _staged.ropeExtRef = extRef;
    _dirty |= D_ROPE;
    taskEXIT_CRITICAL(&_mux);
    markDirty(D_ROPE);
}

void persist_setImu(const PersistImu& imu) {
    taskENTER_CRITICAL(&_mux);
    _staged.imu = imu;
    _dirty |= D_IMU;
    taskEXIT_CRITICAL(&_mux);
    markDirty(D_IMU);
}

void persist_setRotary(uint8_t idx, const PersistRotary& r, bool boomCalibrated) {
    if (idx > 1) return;
    taskENTER_CRITICAL(&_mux);
    _staged.rot[idx] = r;
    _staged.boomCal  = boomCalibrated;
    _dirty |= (D_ROT0 << idx);
    taskEXIT_CRITICAL(&_mux);
    markDirty(D_ROT0 << idx);
}

void persist_setTareOffset(int32_t offset) {
    taskENTER_CRITICAL(&_mux);
    _staged.tare = offset;
    _dirty |= D_TARE;
    taskEXIT_CRITICAL(&_mux);
    markDirty(D_TARE);
}

void persist_setLoadScale(float countsPerKg) {
    taskENTER_CRITICAL(&_mux);
    _staged.loadScale = countsPerKg;
    _dirty |= D_LOADSCALE;
    taskEXIT_CRITICAL(&_mux);
    markDirty(D_LOADSCALE);
}

void persist_setAxisScale(uint8_t idx, float unitsPerRev) {
    if (idx > 1) return;
    taskENTER_CRITICAL(&_mux);
    _staged.axisScale[idx] = unitsPerRev;
    _dirty |= (D_SCALE0 << idx);
    taskEXIT_CRITICAL(&_mux);
    markDirty(D_SCALE0 << idx);
}

void persist_setLoadChart(const LoadChart& chart) {
    taskENTER_CRITICAL(&_mux);
    _staged.chart = chart;
    _dirty |= D_CHART;
    taskEXIT_CRITICAL(&_mux);
    markDirty(D_CHART);
}

uint32_t persist_writes()   { return _writes; }
uint32_t persist_failures() { return _failures; }

// ---------------------------------------------------------------- writer

static bool writeGroup(uint32_t bit, const Staged& s) {
    static const char* POS_TAG[4] = { "sw", "bm", "tl", "rp" };
    Preferences p;
    bool ok = true;

    if (bit & (D_POS0 | D_POS1 | D_POS2 | D_POS3)) {
        const uint8_t slot = (bit == D_POS0) ? 0 : (bit == D_POS1) ? 1 : (bit == D_POS2) ? 2 : 3;
        char kr[8], kc[8], kw[8];
        snprintf(kr, sizeof(kr), "%s_r", POS_TAG[slot]);
        snprintf(kc, sizeof(kc), "%s_c", POS_TAG[slot]);
        snprintf(kw, sizeof(kw), "%s_w", POS_TAG[slot]);
        if (!p.begin("sli_pos2", false)) return false;
        ok = p.putFloat(kr, s.pos[slot].ref) && p.putInt(kc, s.pos[slot].counts);
        if (slot < 3) ok = ok && p.putUShort(kw, s.pos[slot].raw);
    } else if (bit == D_TELE || bit == D_IMU) {
        if (!p.begin("sli_imu", false)) return false;
        if (bit == D_TELE) {
            ok = p.putFloat("tele_scale", s.teleScale) && p.putBool("tele_inv", s.teleInv);
        } else {
            ok = p.putFloat("b_off", s.imu.boomOffset) && p.putFloat("t_off", s.imu.tiltOffset) &&
                 p.putUChar("b_src", s.imu.boomSource) && p.putUChar("t_src", s.imu.tiltSource) &&
                 p.putBool("b_inv", s.imu.boomInvert)  && p.putBool("t_inv", s.imu.tiltInvert);
        }
    } else if (bit == D_ROPE) {
        if (!p.begin("sli_rope", false)) return false;
        ok = p.putFloat("scale", s.ropeScale) && p.putBool("inv", s.ropeInv);
        if (isfinite(s.ropeExtRef)) ok = ok && p.putFloat("ext_ref", s.ropeExtRef);
    } else if (bit == D_ROT0 || bit == D_ROT1) {
        const uint8_t idx = (bit == D_ROT0) ? 0 : 1;
        const char* pre = idx == 0 ? "s" : "b";
        char kz[8], kr[8], ki[8];
        snprintf(kz, sizeof(kz), "%s_z", pre);
        snprintf(kr, sizeof(kr), "%s_r", pre);
        snprintf(ki, sizeof(ki), "%s_i", pre);
        if (!p.begin("sli_enc", false)) return false;
        ok = p.putUShort(kz, s.rot[idx].zeroRaw) && p.putFloat(kr, s.rot[idx].ref) &&
             p.putBool(ki, s.rot[idx].inverted);
        if (idx == 1) ok = ok && p.putBool("b_cal", s.boomCal);
    } else if (bit == D_TARE || bit == D_LOADSCALE) {
        if (!p.begin("sli_hx", false)) return false;
        ok = (bit == D_TARE) ? p.putLong("offset", s.tare) : p.putFloat("scale", s.loadScale);
    } else if (bit == D_SCALE0 || bit == D_SCALE1) {
        if (!p.begin("sli_enc", false)) return false;
        ok = p.putFloat(bit == D_SCALE0 ? "s_s" : "b_s", s.axisScale[bit == D_SCALE0 ? 0 : 1]);
    } else if (bit == D_CHART) {
        if (!p.begin("sli_lc", false)) return false;
        ok = p.putBytes("chart", &s.chart, sizeof(LoadChart)) == sizeof(LoadChart);
    }
    p.end();
    return ok;
}

void storageTask(void* pvParameters) {
    for (;;) {
        // Sleep until something is staged
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        // Let a burst of related changes (e.g. invert + reference) arrive first
        vTaskDelay(pdMS_TO_TICKS(STORAGE_COALESCE_MS));

        Staged   snap;
        uint32_t dirty;
        taskENTER_CRITICAL(&_mux);
        snap   = _staged;
        dirty  = _dirty;
        _dirty = 0;
        taskEXIT_CRITICAL(&_mux);

        for (uint32_t bit = 1; bit && bit <= D_LAST; bit <<= 1) {
            if (!(dirty & bit)) continue;
            if (writeGroup(bit, snap)) {
                _writes = _writes + 1;
            } else {
                _failures = _failures + 1;
                taskENTER_CRITICAL(&_mux);
                _dirty |= bit;            // retry on the next wake
                taskEXIT_CRITICAL(&_mux);
            }
        }
        // Failed groups: try again in a second without waiting for a new change
        taskENTER_CRITICAL(&_mux);
        const bool retry = _dirty != 0;
        taskEXIT_CRITICAL(&_mux);
        if (retry) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            xTaskNotifyGive(xTaskGetCurrentTaskHandle());
        }
    }
}
