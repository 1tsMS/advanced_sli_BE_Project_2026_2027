// ============================================================
//  Sensor Task — Implementation
// ============================================================
#include "sensor_task.h"
#include "boom_check.h"

// Local references to driver instances (set by init)
static AS5600Driver*  _swingEnc  = nullptr;
static AS5600Driver*  _boomEnc   = nullptr;
static AS5600Driver*  _teleEnc   = nullptr;
static MPU6050Driver* _imu       = nullptr;
static HX711Driver*   _loadCell  = nullptr;
static FSRReader*     _fsr       = nullptr;
static N20Encoder*    _winchEnc  = nullptr;

// Global sensor data — declared in esp32_main.ino
extern SensorData sensorData;

void sensorTask_init(
    AS5600Driver* swingEnc,
    AS5600Driver* boomEnc,
    AS5600Driver* teleEnc,
    MPU6050Driver* imu,
    HX711Driver* loadCell,
    FSRReader* fsr,
    N20Encoder* winchEnc
) {
    _swingEnc = swingEnc;
    _boomEnc  = boomEnc;
    _teleEnc  = teleEnc;
    _imu      = imu;
    _loadCell = loadCell;
    _fsr      = fsr;
    _winchEnc = winchEnc;
}

#include <Preferences.h>

static Preferences _prefs;
static float   _boomAngleOffset = 0.0f;
static float   _tiltAngleOffset = 0.0f;
static uint8_t _boomSource      = 0;     // 0 = Roll (X) — physical MPU placement default
static uint8_t _tiltSource      = 1;     // 1 = Pitch (Y) — independent from boom
static bool    _boomInvert      = false;
static bool    _tiltInvert      = false;
static float   _teleScale       = TELE_MM_PER_REVOLUTION;
static bool    _teleInvert      = TELE_DEFAULT_INVERT;

// IMU vs boom-encoder cross-check (boom AS5600 is on the pivot shaft, 1:1)
static BoomAngleCheck   _boomCheck;
static volatile uint8_t _boomCheckState = 0;     // see sensorTask_getBoomCheck()
static volatile float   _boomCheckErr   = 0.0f;

static volatile bool    _reqImuCalibration = false;
static volatile uint8_t _reqBoomSrc = 0;
static volatile uint8_t _reqTiltSrc = 1;
static volatile bool    _reqBoomInv = false;
static volatile bool    _reqTiltInv = false;

// The cross-check reference + learned direction live in their own NVS namespace
// (a local Preferences object, so this is safe to call from any task).
static void saveBoomCheck() {
    Preferences p;
    if (p.begin("sli_bchk", false)) {
        p.putFloat("ref", _boomCheck.refDeg());
        p.putUChar("sign", (uint8_t)(_boomCheck.sign() + 1));   // -1/0/+1 stored as 0/1/2
        p.end();
    }
}

static void loadBoomCheck() {
    float ref = 0.0f;
    int8_t sign = 0;
    Preferences p;
    if (p.begin("sli_bchk", true)) {
        ref  = p.getFloat("ref", 0.0f);
        sign = (int8_t)p.getUChar("sign", 1) - 1;
        p.end();
    }
    _boomCheck.configure(ref, sign);
}

uint8_t sensorTask_getBoomCheck(float* errDeg) {
    if (errDeg) *errDeg = _boomCheckErr;
    return _boomCheckState;
}

void sensorTask_saveCalibration() {
    _prefs.begin("sli_imu", false);
    _prefs.putFloat("b_off",      _boomAngleOffset);
    _prefs.putFloat("t_off",      _tiltAngleOffset);
    _prefs.putUChar("b_src",      _boomSource);
    _prefs.putUChar("t_src",      _tiltSource);
    _prefs.putBool("b_inv",       _boomInvert);
    _prefs.putBool("t_inv",       _tiltInvert);
    _prefs.putFloat("tele_scale", _teleScale);
    _prefs.putBool("tele_inv",    _teleInvert);
    _prefs.end();
}

void sensorTask_loadCalibration() {
    _prefs.begin("sli_imu", true);
    _boomAngleOffset = _prefs.getFloat("b_off",      0.0f);
    _tiltAngleOffset = _prefs.getFloat("t_off",      0.0f);
    _boomSource      = _prefs.getUChar("b_src",      0);   // default: Roll (X)
    _tiltSource      = _prefs.getUChar("t_src",      1);   // default: Pitch (Y)
    _boomInvert      = _prefs.getBool("b_inv",       false);
    _tiltInvert      = _prefs.getBool("t_inv",       false);
    _teleScale       = _prefs.getFloat("tele_scale", TELE_MM_PER_REVOLUTION);
    _teleInvert      = _prefs.getBool("tele_inv",    TELE_DEFAULT_INVERT);
    _prefs.end();
    loadBoomCheck();
}

void sensorTask_resetTelescope(float scale, int8_t invert) {
    bool isTuningOnly = (scale > 0.1f || invert >= 0);
    if (scale > 0.1f) {
        _teleScale = scale;
    }
    if (invert >= 0) {
        _teleInvert = (invert != 0);
    }
    if (!isTuningOnly && _teleEnc) {
        _teleEnc->setZero();
        Serial.println("$ACK,CAL2,TELE_ZEROED");
    } else {
        Serial.print("$ACK,CAL2,TELE_SAVED,SCALE:");
        Serial.print(_teleScale, 3);
        Serial.print(",INV:");
        Serial.println(_teleInvert ? "1" : "0");
    }
    sensorTask_saveCalibration();
}

float sensorTask_getTeleScale()  { return _teleScale; }
bool  sensorTask_getTeleInvert() { return _teleInvert; }

void sensorTask_zeroIMU() {
    if (_imu && _imu->isConnected()) {
        float r = _imu->getRoll();
        float p = _imu->getPitch();
        // Use independently configured source axes for zeroing
        _boomAngleOffset = (_boomSource == 1) ? p : r;
        _tiltAngleOffset = (_tiltSource == 1) ? p : r;
        sensorTask_saveCalibration();

        // The pose the IMU was just zeroed at is also the encoder's reference pose
        if (_boomEnc && _boomEnc->isConnected()) {
            _boomCheck.setReference(_boomEnc->readAngle());
            saveBoomCheck();
        }
    }
}

void sensorTask_requestIMUCalibration(uint8_t boomSrc, bool boomInv, uint8_t tiltSrc, bool tiltInv) {
    _reqBoomSrc = boomSrc;
    _reqBoomInv = boomInv;
    _reqTiltSrc = tiltSrc;
    _reqTiltInv = tiltInv;
    _reqImuCalibration = true;
}

void sensorTask(void* pvParameters) {
    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(SENSOR_PERIOD_MS);

    for (;;) {
        // Process pending IMU calibration strictly on Core 1
        // This prevents I2C bus collision with CommandTask on Core 0
        if (_reqImuCalibration) {
            _reqImuCalibration = false;
            Serial.println("$ACK,DBG,Executing IMU Cal on Core 1...");
            if (_imu && _imu->isConnected()) {
                _imu->calibrateGyro(100);
                Wire.setClock(100000);
            }
            _boomSource = _reqBoomSrc;
            _tiltSource = _reqTiltSrc;
            _boomInvert = _reqBoomInv;
            _tiltInvert = _reqTiltInv;
            sensorTask_zeroIMU();

            Serial.print("$ACK,DBG,IMU Zeroed! b_off:");
            Serial.print(_boomAngleOffset);
            Serial.print(" t_off:");
            Serial.print(_tiltAngleOffset);
            Serial.print(" b_src:");
            Serial.print(_boomSource);
            Serial.print(" t_src:");
            Serial.print(_tiltSource);
            Serial.print(" b_inv:");
            Serial.print(_boomInvert);
            Serial.print(" t_inv:");
            Serial.println(_tiltInvert);
        }

        // ---- Read all sensors ----

        // AS5600 encoders (angles in degrees)
        float swing = _swingEnc ? _swingEnc->readAngle() : 0.0f;
        float tele  = _teleEnc  ? _teleEnc->readContinuousAngle() : 0.0f;

        // MPU6050 IMU on the boom (updates internal complementary filter)
        if (_imu) _imu->update();

        float mpuRoll  = _imu ? _imu->getRoll()  : 0.0f;
        float mpuPitch = _imu ? _imu->getPitch() : 0.0f;

        // Independent axis sources for boom and tilt
        float boomBase = (_boomSource == 1) ? mpuPitch : mpuRoll;
        float tiltBase = (_tiltSource == 1) ? mpuPitch : mpuRoll;

        // Calibrated zero offset subtraction and inversion
        float rawBoom = (boomBase - _boomAngleOffset) * (_boomInvert ? -1.0f : 1.0f);
        float rawTilt = (tiltBase - _tiltAngleOffset) * (_tiltInvert ? -1.0f : 1.0f);

        // The boom angle is only trustworthy from the calibrated IMU. Without it the
        // raw boom encoder angle is shown, but it is not referenced to horizontal, so
        // it is flagged as a fault instead of being fed to the load chart unnoticed.
        const bool  boomOK = (_imu && _imu->isConnected());
        const bool  encOK  = (_boomEnc && _boomEnc->isConnected());
        const float encDeg = encOK ? _boomEnc->readAngle() : 0.0f;
        float boom = boomOK ? rawBoom : encDeg;

        // Cross-check the IMU against the encoder that turns with the boom
        bool boomMismatch = false;
        if (boomOK && encOK) {
            BoomCheckResult r = _boomCheck.update(millis(), encDeg, rawBoom);
            boomMismatch    = (r == BOOM_CHECK_MISMATCH);
            _boomCheckState = (uint8_t)(r == BOOM_CHECK_LEARNING ? 1 : (r == BOOM_CHECK_OK ? 2 : 3));
            _boomCheckErr   = _boomCheck.errorDeg();
            if (_boomCheck.takeLearned()) saveBoomCheck();   // direction just learned
        } else {
            _boomCheckState = 0;   // nothing to compare
            _boomCheckErr   = 0.0f;
        }

        // HX711 load cell (non-blocking — returns cached if not ready)
        float weight = _loadCell ? _loadCell->getWeight() : 0.0f;

        // FSR outrigger sensors (raw ADC)
        uint16_t fsrVals[4] = {0, 0, 0, 0};
        if (_fsr) _fsr->readAll(fsrVals);

        // N20 winch encoder (rope length)
        float ropeLen = _winchEnc ? _winchEnc->getRopeLengthMM() : 0.0f;

        // ---- Convert telescope angle to linear extension ----
        // Full AS5600 rotation (360°) = _teleScale mm (calibrated with direction inversion)
        float rawExtMM = (tele / 360.0f) * _teleScale;
        float extensionMM = _teleInvert ? -rawExtMM : rawExtMM;

        // ---- Write to shared struct under mutex ----
        if (xSemaphoreTake(sensorMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            sensorData.swingAngle   = swing;
            sensorData.boomAngle    = boom;
            sensorData.extensionMM  = extensionMM;
            sensorData.ropeLengthMM = ropeLen;

            // Uncorrected kg straight from the HX711. SafetyTask derives the
            // angle-compensated load, limit, percent and alarm level from it.
            sensorData.loadCellRaw  = weight;
            sensorData.loadSensorOK = _loadCell ? _loadCell->isFresh(LOAD_SENSOR_STALE_MS) : false;

            sensorData.boomLean = rawTilt;  // Sideways lean of the boom (IMU axis other than the boom angle)
            sensorData.boomSensorOK = boomOK;
            sensorData.boomMismatch = boomMismatch;
            sensorData.extSensorOK  = _teleEnc ? _teleEnc->isConnected() : false;

            sensorData.fsr[0] = fsrVals[0];
            sensorData.fsr[1] = fsrVals[1];
            sensorData.fsr[2] = fsrVals[2];
            sensorData.fsr[3] = fsrVals[3];

            // Load %, safe limit and alarm level are computed by SafetyTask

            // System health check
            sensorData.sensorsOK = (
                (_swingEnc ? _swingEnc->isConnected() : true) &&
                (_boomEnc  ? _boomEnc->isConnected()  : true) &&
                (_teleEnc  ? _teleEnc->isConnected()  : true) &&
                (_imu      ? _imu->isConnected()      : true) &&
                (_loadCell ? _loadCell->isConnected()  : true)
            );

            xSemaphoreGive(sensorMutex);
        }

        // Sleep until next period — precise 100Hz timing
        vTaskDelayUntil(&lastWakeTime, period);
    }
}
