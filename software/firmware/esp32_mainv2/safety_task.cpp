// ============================================================
//  SafetyTask — Implementation
// ============================================================
#include "safety_task.h"
#include "safety_logic.h"
#include "config.h"
#include "serial_out.h"
#include "persist.h"
#include <Preferences.h>
#include <string.h>
#include "esp_task_wdt.h"

extern SensorData sensorData;

// ---- Shared with CommandTask (small fields under a spinlock) ----
static portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
static LoadChart    _chart;                 // active chart, valid only if _chartValid
static bool         _chartValid  = false;
static uint32_t     _chartSeq    = 0;       // bumped on every commit
static uint8_t      _blocked     = 0xFF;    // nothing allowed until the first evaluation
static SliState     _state       = SLI_FAULT;
static bool         _estop       = false;
static bool         _bypass      = false;
static uint32_t     _bypassEndMs = 0;
static bool         _off         = false;   // SLI OFF: no motion cut until SLI ON or reboot

// CommandTask-only: chart being uploaded
static LoadChartStage _stage;

static bool bypassActiveLocked(uint32_t now) {
    if (_bypass && (int32_t)(now - _bypassEndMs) >= 0) _bypass = false;   // lapsed
    return _bypass;
}

// ======================== INIT ========================

void safety_init() {
    Preferences p;
    if (!p.begin("sli_lc", true)) return;
    LoadChart c;
    const char* why = "";
    if (p.getBytesLength("chart") == sizeof(LoadChart) &&
        p.getBytes("chart", &c, sizeof(c)) == sizeof(c) &&
        lc_validate(c, &why)) {
        _chart      = c;
        _chartValid = true;
        _chartSeq++;
        Serial.printf("Load chart: %s (%ux%u, %u zones)\n", c.name, c.nA, c.nE, c.nZ);
    } else {
        Serial.println("Load chart: NONE (SLI in FAULT: only boom up / retract / pay out)");
    }
    p.end();
}

// ======================== COMMANDTASK API ========================

bool safety_motionAllowed(uint8_t axis, uint8_t dir, const char** reason) {
    if (axis < 1 || axis > 4) { *reason = "AXIS"; return false; }
    taskENTER_CRITICAL(&_mux);
    const bool     estop   = _estop;
    const bool     bypass  = bypassActiveLocked(millis());
    const bool     off     = _off;
    const uint8_t  blocked = _blocked;
    const SliState state   = _state;
    taskEXIT_CRITICAL(&_mux);

    if (estop)  { *reason = "ESTOP"; return false; }   // E-stop latch holds in every mode
    if (bypass || off) return true;
    if (blocked & sli_bit(axis, dir)) {
        // CUT/FAULT name the state; OK/WARN means a predictive cut or chart edge
        *reason = (state == SLI_CUT || state == SLI_FAULT) ? sli_stateName(state) : "LIMIT";
        return false;
    }
    return true;
}

void safety_estop() {
    taskENTER_CRITICAL(&_mux);
    _estop = true;
    taskEXIT_CRITICAL(&_mux);
}

bool safety_estopLatched() {
    taskENTER_CRITICAL(&_mux);
    const bool e = _estop;
    taskEXIT_CRITICAL(&_mux);
    return e;
}

void safety_reset() {
    taskENTER_CRITICAL(&_mux);
    _estop = false;
    taskEXIT_CRITICAL(&_mux);
}

void safety_bypass(uint16_t seconds) {
    if (seconds > SAFETY_BYPASS_MAX_S) seconds = SAFETY_BYPASS_MAX_S;
    taskENTER_CRITICAL(&_mux);
    _bypass      = seconds > 0;
    _bypassEndMs = millis() + (uint32_t)seconds * 1000UL;
    taskEXIT_CRITICAL(&_mux);
}

void safety_arm() {
    taskENTER_CRITICAL(&_mux);
    _bypass = false;
    _off    = false;
    taskEXIT_CRITICAL(&_mux);
}

void safety_off() {
    taskENTER_CRITICAL(&_mux);
    _off    = true;
    _bypass = false;
    taskEXIT_CRITICAL(&_mux);
}

// ======================== LOAD CHART UPLOAD ========================

static void printFloats(const char* tag, const float* v, uint8_t n, int row) {
    char line[SERIAL_LINE_MAX];
    int pos = (row >= 0) ? snprintf(line, sizeof(line), "$LC,%s,%d", tag, row)
                         : snprintf(line, sizeof(line), "$LC,%s", tag);
    for (uint8_t i = 0; i < n && pos > 0 && pos < (int)sizeof(line); i++) {
        pos += snprintf(line + pos, sizeof(line) - pos, ",%.3f", v[i]);
    }
    serialLine("%s", line);
}

static void printChart() {
    LoadChart c;
    bool valid;
    taskENTER_CRITICAL(&_mux);
    valid = _chartValid;
    if (valid) c = _chart;
    taskEXIT_CRITICAL(&_mux);

    if (!valid) { serialLine("$LC,NONE"); return; }
    serialLine("$LC,NAME,%s", c.name);
    printFloats("A", c.angDeg, c.nA, -1);
    printFloats("E", c.extMm, c.nE, -1);
    for (uint8_t i = 0; i < c.nA; i++) printFloats("R", c.capKg[i], c.nE, i);
    for (uint8_t z = 0; z < c.nZ; z++) {
        serialLine("$LC,Z,%.1f,%.1f,%.3f", c.zones[z].fromDeg, c.zones[z].toDeg, c.zones[z].factor);
    }
    serialLine("$LC,P,%.1f,%.1f", c.warnPct, c.cutPct);
    serialLine("$LC,END");
}

void safety_handleLc(const char* args) {
    const char* why = "";
    switch (lc_parse(&_stage, args, &why)) {
        case LC_PARSE_OK:
            serialLine("$ACK,LC,OK");
            return;
        case LC_PARSE_GET:
            printChart();
            return;
        case LC_PARSE_COMMIT: {
            if (!lc_stageComplete(_stage, &why) || !lc_validate(_stage.chart, &why)) {
                serialLine("$ACK,LC,ERR,%s", why);
                return;
            }
            taskENTER_CRITICAL(&_mux);
            _chart      = _stage.chart;
            _chartValid = true;
            _chartSeq++;
            taskEXIT_CRITICAL(&_mux);
            persist_setLoadChart(_stage.chart);
            serialLine("$ACK,LC,COMMITTED,%s", _stage.chart.name);
            _stage.begun = false;
            return;
        }
        default:
            serialLine("$ACK,LC,ERR,%s", why);
            return;
    }
}

// ======================== TASK ========================

/** "SENSOR:IMU+LOAD" style text for the $S line. */
static void reasonText(SliReason r, uint8_t fault, char* out, size_t len) {
    if (r != SLI_WHY_SENSOR) { snprintf(out, len, "%s", sli_reasonName(r)); return; }
    static const char* NAMES[5] = { "SWING", "BOOM", "TELE", "IMU", "LOAD" };
    int pos = snprintf(out, len, "SENSOR");
    char sep = ':';
    for (uint8_t i = 0; i < 5 && pos > 0 && pos < (int)len; i++) {
        if (fault & (1u << i)) {
            pos += snprintf(out + pos, len - pos, "%c%s", sep, NAMES[i]);
            sep = '+';
        }
    }
}

static uint8_t alarmLevelFor(SliState s, bool estop) {
    if (estop) return 3;
    switch (s) {
        case SLI_OK:   return 0;
        case SLI_WARN: return 1;
        default:       return 2;   // CUT and FAULT
    }
}

void safetyTask(void* pvParameters) {
    TickType_t lastWake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(SAFETY_PERIOD_MS);
    esp_task_wdt_add(NULL);

    LoadChart chart;
    bool      chartValid = false;
    uint32_t  seenSeq    = 0xFFFFFFFF;
    SliState  prevState  = SLI_FAULT;
    uint8_t   prevBlocked = 0xFF;
    bool      prevEstop = false, prevBypass = false, prevOff = false;
    uint32_t  cycle = 0;

    for (;;) {
        esp_task_wdt_reset();

        // Fresh copy of the chart only when a new one was committed
        taskENTER_CRITICAL(&_mux);
        if (_chartSeq != seenSeq) {
            seenSeq    = _chartSeq;
            chartValid = _chartValid;
            if (chartValid) chart = _chart;
        }
        taskEXIT_CRITICAL(&_mux);

        // Pose + load. If the mutex is busy, fail safe: treat the sensors as down.
        SafetyInputs in = {};
        in.sensorsOK = false;
        uint8_t sensorFault = 0;      // mutex busy: sensorsOK stays false, names unknown
        if (xSemaphoreTake(sensorMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            in.boomDeg   = sensorData.boomAngle;
            in.extMm     = sensorData.extensionMM;
            in.swingDeg  = sensorData.swingAngle;
            in.loadKg    = sensorData.actualLoadKg;
            in.sensorsOK = sensorData.sensorsOK;
            sensorFault  = sensorData.sensorFault;
            xSemaphoreGive(sensorMutex);
        }

        const SafetyResult r = sli_evaluate(chartValid ? &chart : nullptr, in, prevState);

        const uint32_t now = millis();
        taskENTER_CRITICAL(&_mux);
        _blocked = r.blocked;
        _state   = r.state;
        const bool estop  = _estop;
        const bool bypass = bypassActiveLocked(now);
        const bool off    = _off;
        const uint32_t bypassLeftS = bypass ? (_bypassEndMs - now + 999) / 1000 : 0;
        taskEXIT_CRITICAL(&_mux);

        if (xSemaphoreTake(sensorMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            sensorData.safeLoadLimit = r.capKg;
            sensorData.loadPercent   = r.pct;
            sensorData.alarmLevel    = alarmLevelFor(r.state, estop);
            xSemaphoreGive(sensorMutex);
        }

        // Newly blocked directions, or the bypass / OFF just ended: CommandTask
        // must stop anything moving that way right now
        if ((r.blocked & ~prevBlocked) || (prevBypass && !bypass) || (prevOff && !off)) {
            if (g_commandTask) xTaskNotifyGive(g_commandTask);
        }

        const bool changed = r.state != prevState || r.blocked != prevBlocked ||
                             estop != prevEstop || bypass != prevBypass || off != prevOff;
        if (changed || cycle % SAFETY_STATUS_EVERY == 0) {
            // Why: "-", NO_CHART, LOAD_RANGE, BAD_VALUE, OUTSIDE, or SENSOR:<names joined by +>
            char why[48];
            reasonText(r.reason, sensorFault, why, sizeof(why));
            // State and blocked mask are what the SLI WOULD do; mode says whether it is enforced
            serialLine("$S,%s,%.1f,%.3f,%d,%lu,%02X,%s,%s,%s",
                       estop ? "ESTOP" : sli_stateName(r.state),
                       r.pct, r.capKg, (int)r.zone, (unsigned long)bypassLeftS,
                       (unsigned)r.blocked, chartValid ? chart.name : "NONE",
                       off ? "OFF" : bypass ? "BYPASS" : "ARMED", why);
        }

        prevState   = r.state;
        prevBlocked = r.blocked;
        prevEstop   = estop;
        prevBypass  = bypass;
        prevOff     = off;
        cycle++;
        vTaskDelayUntil(&lastWake, period);
    }
}
