// ============================================================
//  Sensor Task — Implementation
// ============================================================
#include "sensor_task.h"
#include "encoder_axis.h"
#include "serial_out.h"
#include "persist.h"
#include <Preferences.h>     // reads at boot only; writes go through persist.h
#include "esp_task_wdt.h"

// Local references to driver instances (set by init)
static AS5600Driver*  _enc[ENC_COUNT] = { nullptr, nullptr, nullptr };
static MPU6050Driver* _imu       = nullptr;
static HX711Driver*   _loadCell  = nullptr;
static FSRReader*     _fsr       = nullptr;
static N20Encoder*    _winchEnc  = nullptr;

// Global sensor data — declared in esp32_main.ino
extern SensorData sensorData;

// Load cell counts as failed when no conversion arrived for this long
// (HX711 converts every 100 ms at 10 SPS, 12.5 ms at 80 SPS)
static const uint32_t LOAD_STALE_MS = 500;

// Calibrated encoder axes.  All three now use ENC_LINEAR (multi-turn
// counting) because every AS5600 magnet sits on the motor shaft behind
// a gear reduction, not on the output shaft.
static EncoderAxis _axis[ENC_COUNT] = {
    EncoderAxis(ENC_LINEAR, SWING_DEG_PER_REV),        // 20:1 cycloidal
    EncoderAxis(ENC_LINEAR, BOOM_DEG_PER_REV),         // leadscrew (ratio TBD)
    EncoderAxis(ENC_LINEAR, TELE_MM_PER_REVOLUTION),
};
static const char* AXIS_NAME[ENC_COUNT] = { "SWING", "BOOM", "TELE" };

// Boom angle comes from the boom encoder once it has been zeroed (CAL4 A2);
// until then the IMU is used, as before.
static bool _boomCalibrated = false;

// IMU calibration (boom angle seen by the IMU + sideways lean)
static float   _boomAngleOffset = 0.0f;
static float   _tiltAngleOffset = 0.0f;
static uint8_t _boomSource      = 0;     // 0 = Roll (X) — physical MPU placement default
static uint8_t _tiltSource      = 1;     // 1 = Pitch (Y) — independent from boom
static bool    _boomInvert      = false;
static bool    _tiltInvert      = false;

static volatile bool    _reqImuCalibration = false;
static volatile uint8_t _reqBoomSrc = 0;
static volatile uint8_t _reqTiltSrc = 1;
static volatile bool    _reqBoomInv = false;
static volatile bool    _reqTiltInv = false;

static QueueHandle_t _calQueue = nullptr;

// ---- Load-cell multi-weight calibration ----
// Each CAL7 W<kg> averages LOAD_CAL_SAMPLES conversions (~2 s at 10 SPS) with the
// weight hanging still, then refits counts-per-kg through zero over every point
// recorded so far: scale = sum(c*w) / sum(w^2). Points live in RAM for this
// session; the fitted scale is saved to flash.
static const uint8_t LOAD_CAL_MAX     = 6;
static const uint8_t LOAD_CAL_SAMPLES = 20;
static float   _loadCalKg[LOAD_CAL_MAX];
static double  _loadCalCounts[LOAD_CAL_MAX];
static uint8_t _loadCalN = 0;
static bool    _loadCalPending = false;
static float   _loadCalPendingKg = 0.0f;
static uint32_t _loadCalStartMs = 0;
static bool    _loadScaleCalibrated = false;

// ---- Hardware-I2C bus task (bus 0: swing AS5600 + MPU6050, bus 1: boom AS5600) ----
// The ESP-IDF 4.4 I2C driver inside this Arduino core waits at least 1 s when a
// transfer hangs (Wire.setTimeOut can't shorten it). Motor noise on the long IMU
// cable caused exactly that: a ~2 s freeze of the WHOLE sensor loop, telescope
// included. So the two hardware buses are read by their own task. A hang there
// now only delays swing / boom / IMU; the telescope (soft I2C), load cell, FSRs
// and winch keep updating at 100 Hz. That task only reads raw values; all encoder
// maths, calibration and saving stay in SensorTask (single owner, no races).
struct HwBusShared {
    uint32_t seq[2]   = {0, 0};      // [swing, boom]: bumped on every new raw sample
    uint16_t raw[2]   = {0, 0};
    uint32_t rawMs[2] = {0, 0};      // millis() when that sample was read
    float    imuBoom  = 0.0f;
    float    imuTilt  = 0.0f;
    bool     imuOK    = false;
    AS5600MagnetStatus mag[2] = {};
    bool     magNew[2] = {false, false};
    float    workMaxMs = 0.0f;       // worst hardware-bus cycle since the last DBG
};
static HwBusShared  _hw;
static portMUX_TYPE _hwMux = portMUX_INITIALIZER_UNLOCKED;
static bool          _tarePending = false;
static uint32_t      _tareStartMs = 0;

// ---- Position persistence (NVS "sli_pos2") ----
// Saved: reference value + encoder counts from the reference + raw angle.
// Restoring those keeps the original zero, so invert / scale changes after a
// reboot still act around the true zero (the first version re-anchored at the
// boot position). Applied after the first raw sample arrives.
struct PendingRestore {
    bool     pending = false;
    float    ref     = 0.0f;
    int32_t  counts  = 0;
    uint16_t raw     = 0;
};
static PendingRestore _pendingRestore[ENC_COUNT];
static const char* POS_TAG[ENC_COUNT + 1] = { "sw", "bm", "tl", "rp" };   // + rope

// Idle-save tracking (index ENC_COUNT = rope): save when stable for 1 s and changed
static float    _prevValue[ENC_COUNT + 1]      = {0};
static float    _lastSavedValue[ENC_COUNT + 1] = {0};
static uint32_t _stableMs[ENC_COUNT + 1]       = {0};

// ---- Rope (N20 winch encoder) ----
// rope mm = ref + sign * (count - zero) * scale  = drum side: what the encoder measures,
// equal to the hanging length (load-cell end to knot) at the extension it was referenced at.
// The winch is fixed and the rope runs along the telescope, so every mm of extension pulls
// a mm of rope over the boom tip: hanging = rope - (extension - extRef).
struct RopeCal {
    float   scale  = ROPE_MM_PER_COUNT_DEFAULT;  // mm per PCNT count
    int8_t  sign   = 1;
    float   ref    = 0.0f;
    int32_t zero   = 0;
    float   extRef = NAN;   // telescope extension when the rope was referenced (NAN = unknown: no compensation)
};
static RopeCal _rope;

static int32_t ropeRawCounts() { return (_winchEnc ? _winchEnc->getCount() : 0) - _rope.zero; }
static float   ropeValue()     { return _rope.ref + (float)(_rope.sign * ropeRawCounts()) * _rope.scale; }
/** Rope pulled over the tip since the reference, by extending (mm, 0 if not referenced). */
static float   ropeExtShift(float extMm) { return isfinite(_rope.extRef) ? extMm - _rope.extRef : 0.0f; }

// Loop stages timed for DBG (index into SensorDiag::stageMaxMs)
enum { STAGE_IMUCAL, STAGE_ENC, STAGE_SAVE, STAGE_MAG, STAGE_CAL, STAGE_IMU, STAGE_HX, STAGE_COUNT };
static float _stageMax[STAGE_COUNT] = {0};
static inline void stageEnd(int stage, uint32_t& t) {
    const uint32_t now = micros();
    const float ms = (now - t) / 1000.0f;
    if (ms > _stageMax[stage]) _stageMax[stage] = ms;
    t = now;
}

// Diagnostics (written by this task under sensorMutex)
static SensorDiag _diag;
static float      _loopSumMs  = 0.0f;
static uint32_t   _loopCount  = 0;

void sensorTask_init(
    AS5600Driver* swingEnc,
    AS5600Driver* boomEnc,
    AS5600Driver* teleEnc,
    MPU6050Driver* imu,
    HX711Driver* loadCell,
    FSRReader* fsr,
    N20Encoder* winchEnc
) {
    _enc[ENC_SWING] = swingEnc;
    _enc[ENC_BOOM]  = boomEnc;
    _enc[ENC_TELE]  = teleEnc;
    _imu      = imu;
    _loadCell = loadCell;
    _fsr      = fsr;
    _winchEnc = winchEnc;
    _calQueue = xQueueCreate(8, sizeof(CalRequest));
    memset(&_diag, 0, sizeof(_diag));
}

// ======================== FLASH (NVS) ========================
// These only STAGE the values (a few bytes under a spinlock); StorageTask on
// core 0 does the actual flash write later. See persist.h for why.

static void saveImuAndTele() {
    PersistImu imu;
    imu.boomOffset = _boomAngleOffset;
    imu.tiltOffset = _tiltAngleOffset;
    imu.boomSource = _boomSource;
    imu.tiltSource = _tiltSource;
    imu.boomInvert = _boomInvert;
    imu.tiltInvert = _tiltInvert;
    persist_setImu(imu);
    persist_setTele(_axis[ENC_TELE].unitsPerRev(), _axis[ENC_TELE].inverted());
}

static void saveRotary(uint8_t idx) {
    PersistRotary r;
    r.zeroRaw  = _axis[idx].zeroRaw();
    r.ref      = _axis[idx].reference();
    r.inverted = _axis[idx].inverted();
    persist_setRotary(idx == ENC_SWING ? 0 : 1, r, _boomCalibrated);
}

// Save an axis position for boot recovery (idx ENC_COUNT = rope).
static void savePosition(uint8_t idx) {
    PersistPos pos;
    if (idx < ENC_COUNT) {
        pos.ref    = _axis[idx].reference();
        pos.counts = _axis[idx].rawCountsFromReference();
        pos.raw    = _axis[idx].lastRaw();
    } else {
        pos.ref    = _rope.ref;
        pos.counts = ropeRawCounts();
        pos.raw    = 0;
    }
    persist_setPosition(idx, pos);
}

static void saveRope() {
    persist_setRope(_rope.scale, _rope.sign < 0, _rope.extRef);
}

void sensorTask_loadCalibration() {
    Preferences p;
    p.begin("sli_imu", true);
    _boomAngleOffset = p.getFloat("b_off", 0.0f);
    _tiltAngleOffset = p.getFloat("t_off", 0.0f);
    _boomSource      = p.getUChar("b_src", 0);   // default: Roll (X)
    _tiltSource      = p.getUChar("t_src", 1);   // default: Pitch (Y)
    _boomInvert      = p.getBool("b_inv", false);
    _tiltInvert      = p.getBool("t_inv", false);
    const float teleScale = p.getFloat("tele_scale", TELE_MM_PER_REVOLUTION);
    const bool  teleInv   = p.getBool("tele_inv",    TELE_DEFAULT_INVERT);
    p.end();
    _axis[ENC_TELE].configure(teleInv ? -1 : 1, 0, 0.0f, teleScale);

    p.begin("sli_enc", true);
    _axis[ENC_SWING].configure(p.getBool("s_i", false) ? -1 : 1,
                               p.getUShort("s_z", 0), p.getFloat("s_r", 0.0f),
                               p.getFloat("s_s", SWING_DEG_PER_REV));
    _axis[ENC_BOOM].configure(p.getBool("b_i", false) ? -1 : 1,
                              p.getUShort("b_z", 0), p.getFloat("b_r", 0.0f),
                              p.getFloat("b_s", BOOM_DEG_PER_REV));
    _boomCalibrated = p.getBool("b_cal", false);
    p.end();

    // ---- Rope scale / direction ----
    p.begin("sli_rope", true);
    const float ropeScale = p.getFloat("scale", ROPE_MM_PER_COUNT_DEFAULT);
    _rope.scale = (ropeScale > 0.0f) ? ropeScale : ROPE_MM_PER_COUNT_DEFAULT;
    _rope.sign  = p.getBool("inv", false) ? -1 : 1;
    _rope.extRef = p.getFloat("ext_ref", NAN);   // absent until the rope is re-referenced
    p.end();

    // ---- Saved positions (encoders are restored after their first sample) ----
    p.begin("sli_pos2", true);
    for (uint8_t i = 0; i <= ENC_COUNT; i++) {
        char kr[8], kc[8], kw[8];
        snprintf(kr, sizeof(kr), "%s_r", POS_TAG[i]);
        snprintf(kc, sizeof(kc), "%s_c", POS_TAG[i]);
        snprintf(kw, sizeof(kw), "%s_w", POS_TAG[i]);
        if (!p.isKey(kc)) continue;
        if (i < ENC_COUNT) {
            _pendingRestore[i].pending = true;
            _pendingRestore[i].ref     = p.getFloat(kr, 0.0f);
            _pendingRestore[i].counts  = p.getInt(kc, 0);
            _pendingRestore[i].raw     = p.getUShort(kw, 0);
        } else {
            // The PCNT count is 0 at boot, so the rope is restored right away.
            // (A winch that unwound while powered off can't be detected.)
            _rope.ref  = p.getFloat(kr, 0.0f);
            _rope.zero = -p.getInt(kc, 0);
            _lastSavedValue[ENC_COUNT] = _prevValue[ENC_COUNT] = ropeValue();
        }
    }
    p.end();

    // ---- Load saved tare offset and scale for HX711 ----
    p.begin("sli_hx", true);
    if (p.isKey("scale") && _loadCell) {
        const float sc = p.getFloat("scale", HX711_DEFAULT_SCALE);
        if (sc != 0.0f) {
            _loadCell->setScale(sc);
            _loadScaleCalibrated = true;
        }
    }
    if (p.isKey("offset") && _loadCell) {
        int32_t savedOffset = p.getLong("offset", 0);
        if (savedOffset != 0) {
            _loadCell->setOffset(savedOffset);
            serialLine("  HX711: restored tare offset %ld from NVS", (long)savedOffset);
        }
    }
    bool hasTare = p.isKey("offset");
    p.end();

    // If no saved tare exists and the load cell is connected, do a
    // fallback tare now (blocking, but only runs on very first boot).
    if (!hasTare && _loadCell && _loadCell->isConnected()) {
        _loadCell->startTare(10);
        _tarePending = true;
        _tareStartMs = millis();
        serialLine("  HX711: no saved offset, auto-tare started");
    }
}

// ======================== REQUESTS FROM OTHER TASKS ========================

void sensorTask_requestIMUCalibration(uint8_t boomSrc, bool boomInv, uint8_t tiltSrc, bool tiltInv) {
    _reqBoomSrc = boomSrc;
    _reqBoomInv = boomInv;
    _reqTiltSrc = tiltSrc;
    _reqTiltInv = tiltInv;
    _reqImuCalibration = true;
}

bool sensorTask_requestCal(const CalRequest& req) {
    return _calQueue && xQueueSend(_calQueue, &req, 0) == pdTRUE;
}

void sensorTask_getDiag(SensorDiag* out, bool resetPeaks) {
    if (xSemaphoreTake(sensorMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        *out = _diag;
        if (resetPeaks) {
            _diag.loopMaxMs   = 0.0f;
            _diag.periodMaxMs = 0.0f;
            for (int k = 0; k < STAGE_COUNT; k++) _stageMax[k] = 0.0f;
            taskENTER_CRITICAL(&_hwMux);
            _hw.workMaxMs = 0.0f;
            taskEXIT_CRITICAL(&_hwMux);
        }
        xSemaphoreGive(sensorMutex);
    } else {
        memset(out, 0, sizeof(*out));
    }
}

// ======================== CALIBRATION (runs on core 1) ========================

// IMU zeroing is a small state machine run by HwBusTask every cycle: the gyro
// samples are collected by the driver's normal 100 Hz update (~1 s), so nothing
// blocks. While it runs the IMU angles hold their last value.
static bool _imuCalRunning = false;

static void runImuCalibration() {
    if (_reqImuCalibration && !_imuCalRunning) {
        _reqImuCalibration = false;
        if (!_imu || !_imu->isConnected()) {
            serialLine("$ACK,CAL1,ERR,IMU_NOT_FOUND");
            return;
        }
        _boomSource = _reqBoomSrc;
        _tiltSource = _reqTiltSrc;
        _boomInvert = _reqBoomInv;
        _tiltInvert = _reqTiltInv;
        _imu->startGyroCalibration(100);   // keep the boom still for ~1 s
        _imuCalRunning = true;
        serialLine("$ACK,CAL1,STARTED");
        return;
    }
    if (!_imuCalRunning || !_imu || !_imu->takeGyroCalibrationDone()) return;
    _imuCalRunning = false;

    const float r = _imu->getRoll();
    const float p = _imu->getPitch();
    _boomAngleOffset = (_boomSource == 1) ? p : r;
    _tiltAngleOffset = (_tiltSource == 1) ? p : r;
    saveImuAndTele();
    serialLine("$ACK,CAL1,IMU_ZEROED,b_off:%.2f t_off:%.2f b_src:%u t_src:%u b_inv:%u t_inv:%u",
               _boomAngleOffset, _tiltAngleOffset, _boomSource, _tiltSource,
               _boomInvert ? 1 : 0, _tiltInvert ? 1 : 0);
}

static void runCal(const CalRequest& req) {
    switch (req.type) {
        case CAL_TARE_LOAD:
            if (!_loadCell || !_loadCell->isConnected()) {
                serialLine("$ACK,CAL0,ERR,HX711_NOT_FOUND");
                return;
            }
            _loadCell->startTare(10);          // finishes over the next ~1 s of samples
            _tarePending = true;
            _tareStartMs = millis();
            return;

        case CAL_RESET_TELE: {
            EncoderAxis& tele = _axis[ENC_TELE];
            const bool tuning = (req.value > 0.1f || req.flag >= 0);
            if (!tuning) {
                if (!tele.hasSample()) { serialLine("$ACK,CAL2,ERR,NO_SAMPLE"); return; }
                tele.setReference(0.0f);
                savePosition(ENC_TELE);
                _lastSavedValue[ENC_TELE] = 0.0f;
                serialLine("$ACK,CAL2,TELE_ZEROED");
                return;
            }
            if (req.value > 0.1f) tele.setUnitsPerRev(req.value);
            if (req.flag >= 0)    tele.setInvert(req.flag != 0);
            saveImuAndTele();
            serialLine("$ACK,CAL2,TELE_SAVED,SCALE:%.4f,INV:%u", tele.unitsPerRev(), tele.inverted() ? 1 : 0);
            return;
        }

        case CAL_ENC_REF: {
            if (req.axis == CAL_AXIS_ROPE) {
                _rope.zero = _winchEnc ? _winchEnc->getCount() : 0;
                _rope.ref  = req.value;
                // Remember the extension this length belongs to (hook-drop compensation)
                if (_axis[ENC_TELE].hasSample()) _rope.extRef = _axis[ENC_TELE].value();
                savePosition(ENC_COUNT);
                saveRope();
                _lastSavedValue[ENC_COUNT] = req.value;
                serialLine("$ACK,CAL4,A4,ROPE,OK,VALUE:%.2f,EXT_REF:%.1f", ropeValue(), _rope.extRef);
                return;
            }
            if (req.axis >= ENC_COUNT) { serialLine("$ACK,CAL4,ERR,BAD_AXIS"); return; }
            EncoderAxis& ax = _axis[req.axis];
            if (!ax.hasSample()) { serialLine("$ACK,CAL4,A%u,ERR,NO_SAMPLE", req.axis + 1); return; }
            ax.setReference(req.value);
            if (req.axis == ENC_BOOM) _boomCalibrated = true;
            if (req.axis != ENC_TELE) saveRotary(req.axis);
            savePosition(req.axis);
            _lastSavedValue[req.axis] = ax.value();
            serialLine("$ACK,CAL4,A%u,%s,OK,VALUE:%.2f", req.axis + 1, AXIS_NAME[req.axis], ax.value());
            return;
        }

        case CAL_ENC_INVERT: {
            if (req.flag < 0) { serialLine("$ACK,CAL5,ERR,BAD_ARGS"); return; }
            if (req.axis == CAL_AXIS_ROPE) {
                _rope.sign = req.flag ? -1 : 1;
                saveRope();
                serialLine("$ACK,CAL5,A4,ROPE,OK,INV:%u", req.flag ? 1 : 0);
                return;
            }
            if (req.axis >= ENC_COUNT) { serialLine("$ACK,CAL5,ERR,BAD_AXIS"); return; }
            _axis[req.axis].setInvert(req.flag != 0);
            if (req.axis == ENC_TELE) saveImuAndTele();
            else                      saveRotary(req.axis);
            serialLine("$ACK,CAL5,A%u,%s,OK,INV:%u", req.axis + 1, AXIS_NAME[req.axis], req.flag ? 1 : 0);
            return;
        }

        case CAL_LOAD: {
            if (!_loadCell || !_loadCell->isConnected()) { serialLine("$ACK,CAL7,ERR,HX711_NOT_FOUND"); return; }
            if (req.axis == 1) {                          // CLEAR
                _loadCalN = 0;
                serialLine("$ACK,CAL7,CLEARED");
                return;
            }
            if (req.axis == 2) {                          // manual scale
                _loadCell->setScale(req.value);
                _loadScaleCalibrated = true;
                persist_setLoadScale(req.value);
                serialLine("$ACK,CAL7,SCALE_SET,SCALE:%.1f", req.value);
                return;
            }
            if (_loadCalPending || _loadCell->averageBusy() || _loadCell->tareBusy()) {
                serialLine("$ACK,CAL7,ERR,BUSY");
                return;
            }
            if (!(req.value > 0.0f && req.value <= MAX_VALID_LOAD_KG)) { serialLine("$ACK,CAL7,ERR,WEIGHT_RANGE"); return; }
            _loadCell->startAverage(LOAD_CAL_SAMPLES);
            _loadCalPending   = true;
            _loadCalPendingKg = req.value;
            _loadCalStartMs   = millis();
            serialLine("$ACK,CAL7,MEASURING,KG:%.3f", req.value);
            return;
        }

        case CAL_AXIS_SCALE: {
            if (req.axis >= ENC_COUNT || !(req.value > 0.0f)) { serialLine("$ACK,CAL8,ERR,BAD_ARGS"); return; }
            EncoderAxis& ax = _axis[req.axis];
            ax.setUnitsPerRev(req.value);
            if (req.flag >= 0) ax.setInvert(req.flag != 0);
            if (req.axis == ENC_TELE) {
                saveImuAndTele();
            } else {
                persist_setAxisScale(req.axis, req.value);
                saveRotary(req.axis);
            }
            serialLine("$ACK,CAL8,A%u,%s,OK,SCALE:%.4f,INV:%u", req.axis + 1, AXIS_NAME[req.axis],
                       ax.unitsPerRev(), ax.inverted() ? 1 : 0);
            return;
        }

        case CAL_ROPE_SCALE: {
            if (!(req.value > 0.0f && req.value < 10.0f)) { serialLine("$ACK,CAL6,ERR,RANGE"); return; }
            _rope.scale = req.value;
            saveRope();
            serialLine("$ACK,CAL6,ROPE_SAVED,SCALE:%.6f,INV:%u", _rope.scale, _rope.sign < 0 ? 1 : 0);
            return;
        }

        default:
            return;
    }
}

// ======================== HARDWARE-I2C BUS TASK ========================

void sensorTask_hwBus(void* pvParameters) {
    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(HWBUS_PERIOD_MS);
    esp_task_wdt_add(NULL);          // a stuck bus task resets the ESP32
    uint32_t cycle = 0;

    for (;;) {
        const uint32_t t0 = micros();

        // IMU zeroing touches bus 0, so it runs here
        esp_task_wdt_reset();
        runImuCalibration();             // non-blocking state machine

        // Swing (bus 0) and boom (bus 1) encoders: raw angle only
        for (uint8_t i = 0; i < 2; i++) {
            uint16_t raw;
            if (_enc[i] && _enc[i]->readRaw(&raw)) {
                const uint32_t ms = millis();
                taskENTER_CRITICAL(&_hwMux);
                _hw.raw[i]   = raw;
                _hw.rawMs[i] = ms;
                _hw.seq[i]++;
                taskEXIT_CRITICAL(&_hwMux);
            }
        }

        // Magnet health: one of the two every 20 cycles
        if (cycle % 20 == 0) {
            const uint8_t m = (cycle / 20) % 2;
            AS5600MagnetStatus s;
            if (_enc[m] && _enc[m]->readMagnetStatus(&s)) {
                taskENTER_CRITICAL(&_hwMux);
                _hw.mag[m]    = s;
                _hw.magNew[m] = true;
                taskEXIT_CRITICAL(&_hwMux);
            }
        }
        cycle++;

        // MPU6050 on the boom (complementary filter inside the driver)
        if (_imu) _imu->update();
        const bool  imuOK    = _imu && _imu->isConnected();
        const float mpuRoll  = imuOK ? _imu->getRoll()  : 0.0f;
        const float mpuPitch = imuOK ? _imu->getPitch() : 0.0f;
        const float boomBase = (_boomSource == 1) ? mpuPitch : mpuRoll;
        const float tiltBase = (_tiltSource == 1) ? mpuPitch : mpuRoll;
        // Wrap after removing the zero: an offset near 180 (roll source, sensor
        // upside down) would otherwise read -359 one sample and +1 the next
        const float imuBoom  = wrapDeg180(boomBase - _boomAngleOffset) * (_boomInvert ? -1.0f : 1.0f);
        const float imuTilt  = wrapDeg180(tiltBase - _tiltAngleOffset) * (_tiltInvert ? -1.0f : 1.0f);

        const float workMs = (micros() - t0) / 1000.0f;
        taskENTER_CRITICAL(&_hwMux);
        _hw.imuBoom = imuBoom;
        _hw.imuTilt = imuTilt;
        _hw.imuOK   = imuOK;
        if (workMs > _hw.workMaxMs) _hw.workMaxMs = workMs;
        taskEXIT_CRITICAL(&_hwMux);

        vTaskDelayUntil(&lastWakeTime, period);
    }
}

// ======================== TASK ========================

void sensorTask(void* pvParameters) {
    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(SENSOR_PERIOD_MS);
    esp_task_wdt_add(NULL);          // a stuck measurement loop resets the ESP32
    uint32_t prevStartUs = micros();
    uint32_t cycle       = 0;

    for (;;) {
        esp_task_wdt_reset();
        const uint32_t startUs = micros();
        const float periodMs = (startUs - prevStartUs) / 1000.0f;
        prevStartUs = startUs;

        uint32_t st = startUs;
        stageEnd(STAGE_IMUCAL, st);

        // ---- Snapshot of the hardware-I2C task's latest readings ----
        static uint32_t seenSeq[2] = {0, 0};
        HwBusShared hw;
        taskENTER_CRITICAL(&_hwMux);
        hw = _hw;
        _hw.magNew[0] = _hw.magNew[1] = false;
        taskEXIT_CRITICAL(&_hwMux);

        // ---- Encoders: swing / boom from the bus task, telescope read here ----
        const uint32_t nowMs = millis();
        for (uint8_t i = 0; i < 2; i++) {
            if (hw.seq[i] != seenSeq[i]) {
                seenSeq[i] = hw.seq[i];
                _axis[i].update(hw.raw[i], hw.rawMs[i]);
            }
        }
        {
            uint16_t raw;
            if (_enc[ENC_TELE] && _enc[ENC_TELE]->readRaw(&raw)) _axis[ENC_TELE].update(raw, nowMs);
        }
        if (_winchEnc) _winchEnc->poll();

        stageEnd(STAGE_ENC, st);

        // ---- Deferred position restore (once per axis, after its first sample) ----
        for (uint8_t i = 0; i < ENC_COUNT; i++) {
            if (_pendingRestore[i].pending && _axis[i].hasSample()) {
                _axis[i].restore(_pendingRestore[i].ref, _pendingRestore[i].counts, _pendingRestore[i].raw);
                _prevValue[i] = _lastSavedValue[i] = _axis[i].value();
                _stableMs[i]  = nowMs;
                _pendingRestore[i].pending = false;
            }
        }

        // ---- Idle-save: persist linear positions when stable for 1 s ----
        for (uint8_t i = 0; i <= ENC_COUNT; i++) {
            // Never save before the axis has a real sample and its saved position
            // has been restored: that would overwrite the good value with a default.
            if (i < ENC_COUNT && (!_axis[i].hasSample() || _pendingRestore[i].pending)) continue;
            const float val = (i < ENC_COUNT) ? _axis[i].value() : ropeValue();
            if (fabsf(val - _prevValue[i]) > 0.5f) {
                _stableMs[i] = nowMs;       // still moving
                _prevValue[i] = val;
            } else if (nowMs - _stableMs[i] > 1000
                       && fabsf(val - _lastSavedValue[i]) > 0.5f) {
                savePosition(i);
                _lastSavedValue[i] = val;
            }
        }

        stageEnd(STAGE_SAVE, st);

        // Magnet health of the telescope encoder (~1.7x per second); swing/boom come from the bus task
        AS5600MagnetStatus teleMag;
        const bool teleMagRead = (cycle % 60 == 0) && _enc[ENC_TELE] && _enc[ENC_TELE]->readMagnetStatus(&teleMag);
        cycle++;

        stageEnd(STAGE_MAG, st);

        // Calibration requests run after the encoders have a fresh sample
        CalRequest req;
        while (_calQueue && xQueueReceive(_calQueue, &req, 0) == pdTRUE) runCal(req);

        stageEnd(STAGE_CAL, st);

        // ---- IMU angles from the bus task ----
        // While the IMU is re-initialising (imuOK false) the bus task reports
        // 0 - offset, so hold the last good angles instead of passing that on.
        // sensorsOK goes false meanwhile, so the hold is never silent.
        static float heldImuBoom = 0.0f, heldImuTilt = 0.0f;
        const bool imuOK = hw.imuOK;
        if (imuOK) { heldImuBoom = hw.imuBoom; heldImuTilt = hw.imuTilt; }
        const float imuBoom = heldImuBoom;
        const float imuTilt = heldImuTilt;

        // Boom angle: IMU by default. The encoder is only used when
        // BOOM_ANGLE_FROM_ENCODER is enabled (see config.h: leadscrew is non-linear).
        // (The old fallback to the uncalibrated boom encoder on an IMU dropout
        // made the angle jump for the ~0.5 s re-init.)
        const bool boomEncOK = BOOM_ANGLE_FROM_ENCODER && _boomCalibrated && _enc[ENC_BOOM] &&
                               _enc[ENC_BOOM]->isConnected() && _axis[ENC_BOOM].hasSample();
        const float boom = boomEncOK ? _axis[ENC_BOOM].value() : imuBoom;

        stageEnd(STAGE_IMU, st);

        // ---- HX711 load cell (non-blocking: one conversion at most) ----
        const float weight = _loadCell ? _loadCell->getWeight() : 0.0f;
        if (_tarePending && _loadCell) {
            if (_loadCell->takeTareDone()) {
                _tarePending = false;
                // Saved by StorageTask so it survives reboots
                persist_setTareOffset(_loadCell->getOffset());
                serialLine("$ACK,CAL0,TARE_DONE");
            } else if (millis() - _tareStartMs > 3000) {
                _tarePending = false;
                serialLine("$ACK,CAL0,ERR,TIMEOUT");
            }
        }

        if (_loadCalPending && _loadCell) {
            double counts;
            if (_loadCell->takeAverageDone(&counts)) {
                _loadCalPending = false;
                if (fabs(counts) < 1000.0) {
                    serialLine("$ACK,CAL7,ERR,NO_LOAD_SEEN (tare first, then hang the weight)");
                } else {
                    // Replace a point recorded with (nearly) the same weight, else append
                    uint8_t slot = _loadCalN;
                    for (uint8_t k = 0; k < _loadCalN; k++) {
                        if (fabsf(_loadCalKg[k] - _loadCalPendingKg) < 0.005f) { slot = k; break; }
                    }
                    if (slot == _loadCalN && _loadCalN < LOAD_CAL_MAX) _loadCalN++;
                    if (slot < LOAD_CAL_MAX) {
                        _loadCalKg[slot]     = _loadCalPendingKg;
                        _loadCalCounts[slot] = counts;
                    }
                    double sxy = 0.0, sxx = 0.0;
                    for (uint8_t k = 0; k < _loadCalN; k++) {
                        sxy += _loadCalCounts[k] * _loadCalKg[k];
                        sxx += (double)_loadCalKg[k] * _loadCalKg[k];
                    }
                    const float scale = (float)(sxy / sxx);             // counts per kg
                    float worstG = 0.0f;
                    for (uint8_t k = 0; k < _loadCalN; k++) {
                        const float errG = fabsf((float)(_loadCalCounts[k] / scale) - _loadCalKg[k]) * 1000.0f;
                        if (errG > worstG) worstG = errG;
                    }
                    _loadCell->setScale(scale);
                    _loadScaleCalibrated = true;
                    persist_setLoadScale(scale);
                    serialLine("$ACK,CAL7,OK,POINTS:%u,SCALE:%.1f,WORST_ERR_G:%.1f",
                               _loadCalN, scale, worstG);
                }
            } else if (millis() - _loadCalStartMs > 5000) {
                _loadCalPending = false;
                serialLine("$ACK,CAL7,ERR,TIMEOUT");
            }
        }

        stageEnd(STAGE_HX, st);

        // ---- FSR outrigger sensors (raw ADC) ----
        uint16_t fsrVals[4] = {0, 0, 0, 0};
        if (_fsr) _fsr->readAll(fsrVals);

        // ---- N20 winch encoder (rope length) ----
        const float ropeLen = ropeValue();

        const float workMs = (micros() - startUs) / 1000.0f;

        // ---- Write to shared struct under mutex ----
        if (xSemaphoreTake(sensorMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            sensorData.swingAngle   = _axis[ENC_SWING].value();
            sensorData.boomAngle    = boom;
            sensorData.teleAngle    = (float)_axis[ENC_TELE].countsFromReference() * 360.0f / EncoderAxis::COUNTS_PER_REV;
            sensorData.extensionMM  = _axis[ENC_TELE].value();
            // Total rope is fixed by the drum; the hanging part shrinks as the telescope extends
            const float ropeHang = ropeLen - ropeExtShift(sensorData.extensionMM);
            sensorData.ropeLengthMM = ropeHang;
            sensorData.ropeTotalMM  = ropeLen + ROPE_FIXED_PATH_MM + ROPE_KNOT_TO_HOOK_MM;
            sensorData.hookDropMM   = ropeHang + ROPE_KNOT_TO_HOOK_MM;

            sensorData.loadCellRaw  = weight;   // kg, uncorrected
            sensorData.actualLoadKg = weight;   // Phase 3: will add angle compensation

            sensorData.imuRoll  = imuBoom;      // IMU's view of the boom angle (cross-check)
            sensorData.imuPitch = imuTilt;      // sideways lean

            sensorData.fsr[0] = fsrVals[0];
            sensorData.fsr[1] = fsrVals[1];
            sensorData.fsr[2] = fsrVals[2];
            sensorData.fsr[3] = fsrVals[3];

            // safeLoadLimit / loadPercent / alarmLevel belong to SafetyTask

            // System health check (cached flags only: no bus traffic here)
            sensorData.sensorsOK = (
                (_enc[ENC_SWING] ? _enc[ENC_SWING]->isConnected() : true) &&
                (_enc[ENC_BOOM]  ? _enc[ENC_BOOM]->isConnected()  : true) &&
                (_enc[ENC_TELE]  ? _enc[ENC_TELE]->isConnected()  : true) &&
                (_imu            ? imuOK                          : true) &&
                // A dead HX711 never clears isConnected(): it just stops converting
                // and getWeight() keeps returning the last value. Stale = fault.
                (_loadCell       ? (_loadCell->isConnected() && _loadCell->isFresh(LOAD_STALE_MS)) : true)
            );

            // Diagnostics for DBG
            for (uint8_t i = 0; i < ENC_COUNT; i++) {
                EncoderDiag& d = _diag.enc[i];
                d.connected = _enc[i] ? _enc[i]->isConnected() : false;
                d.reads     = _enc[i] ? _enc[i]->readCount() : 0;
                d.fails     = _enc[i] ? _enc[i]->failCount() : 0;
                d.rejected  = _axis[i].rejected();
                d.resyncs   = _axis[i].resyncs();
                d.inverted  = _axis[i].inverted();
                d.reference = _axis[i].reference();
                d.scale     = _axis[i].unitsPerRev();
            }
            if (teleMagRead)  _diag.enc[ENC_TELE].magnet  = teleMag;
            if (hw.magNew[0]) _diag.enc[ENC_SWING].magnet = hw.mag[0];
            if (hw.magNew[1]) _diag.enc[ENC_BOOM].magnet  = hw.mag[1];
            _diag.teleScale       = _axis[ENC_TELE].unitsPerRev();
            _diag.ropeCounts      = _rope.sign * ropeRawCounts();
            _diag.ropeScale       = _rope.scale;
            _diag.ropeInverted    = _rope.sign < 0;
            _diag.boomCalibrated  = _boomCalibrated;
            _diag.boomFromEncoder = boomEncOK;
            _diag.hx711Raw        = _loadCell ? _loadCell->getLastRaw() : 0;
            _diag.loadScale       = _loadCell ? _loadCell->getScale() : 0.0f;
            _diag.loadPoints      = _loadCalN;
            _diag.loadScaleCalibrated = _loadScaleCalibrated;
            _loopSumMs += workMs;
            if (++_loopCount >= 100) {
                _diag.loopAvgMs = _loopSumMs / _loopCount;
                _loopSumMs = 0.0f;
                _loopCount = 0;
            }
            if (workMs > _diag.loopMaxMs)     _diag.loopMaxMs   = workMs;
            if (periodMs > _diag.periodMaxMs) _diag.periodMaxMs = periodMs;
            if (hw.workMaxMs > _stageMax[STAGE_IMU]) _stageMax[STAGE_IMU] = hw.workMaxMs;   // "hwbus" in DBG
            for (int k = 0; k < STAGE_COUNT; k++) _diag.stageMaxMs[k] = _stageMax[k];
            _diag.imuReconnects = _imu ? _imu->reconnects() : 0;
            _diag.imuRejected   = _imu ? _imu->rejectedSamples() : 0;

            xSemaphoreGive(sensorMutex);
        }

        // Sleep until next period — precise 100Hz timing
        vTaskDelayUntil(&lastWakeTime, period);
    }
}
