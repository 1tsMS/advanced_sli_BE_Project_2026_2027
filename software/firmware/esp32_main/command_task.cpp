// ============================================================
//  Command Task — Implementation
// ============================================================
#include "command_task.h"
#include "telemetry_task.h"
#include "sensor_task.h"

// Subsystem references (set by init)
static MegaBridge*   _mega      = nullptr;
static N20Encoder*   _winch     = nullptr;
static HX711Driver*  _loadCell  = nullptr;
static MPU6050Driver* _imu      = nullptr;
static AS5600Driver* _teleEnc   = nullptr;
static AS5600Driver* _swingEnc  = nullptr;
static AS5600Driver* _boomEnc   = nullptr;
static LoadChart*    _chart     = nullptr;
static LoadCompensation* _comp  = nullptr;

// Global sensor data — declared in esp32_main.ino
extern SensorData sensorData;

// Winch dead-man state (the winch is driven directly by the ESP32)
static bool          _winchActive    = false;
static unsigned long _lastWinchCmdMs = 0;

// Only report "blocked by E-stop" once per latch so a held button doesn't flood the log
static bool _blockedReported = false;

/** Map a G-code speed (0..MOTOR_SPEED_MAX) onto the winch's 0..255 PWM range. */
static inline uint8_t speedToPwm(uint16_t speed) {
    if (speed > MOTOR_SPEED_MAX) speed = MOTOR_SPEED_MAX;
    return (uint8_t)(((uint32_t)speed * 255UL) / MOTOR_SPEED_MAX);
}

/** Reply to an LC command. One print() per line so telemetry can't split it. */
static void handleLoadChart(const ParsedCommand& cmd) {
    char buf[80];

    if (!_chart) {
        Serial.print("$LC,ERR,UNAVAILABLE\n");
        return;
    }

    switch (cmd.lcOp) {
        case LC_OP_UPLOAD: {
            LoadChartStatus s = _chart->beginUpload(cmd.lcCount);
            if (s == LC_OK) snprintf(buf, sizeof(buf), "$LC,READY,%u\n", (unsigned)cmd.lcCount);
            else            snprintf(buf, sizeof(buf), "$LC,ERR,%s\n", LoadChart::statusName(s));
            Serial.print(buf);
            break;
        }
        case LC_OP_ENTRY: {
            // Entries are not acknowledged one by one; only failures are reported
            LoadChartStatus s = _chart->addEntry(cmd.lcAngle, cmd.lcExtension, cmd.lcLimit);
            if (s != LC_OK) {
                snprintf(buf, sizeof(buf), "$LC,ERR,%s\n", LoadChart::statusName(s));
                Serial.print(buf);
            }
            break;
        }
        case LC_OP_SAVE: {
            LoadChartStatus s = _chart->commit();
            if (s == LC_OK) snprintf(buf, sizeof(buf), "$LC,OK,%u\n", (unsigned)_chart->count());
            else            snprintf(buf, sizeof(buf), "$LC,ERR,%s\n", LoadChart::statusName(s));
            Serial.print(buf);
            break;
        }
        case LC_OP_GET: {
            // Snapshot under the mutex, then print without holding it
            LoadChartEntry entries[LC_MAX_ENTRIES];
            uint8_t n = 0;
            if (xSemaphoreTake(sensorMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                n = _chart->count();
                for (uint8_t i = 0; i < n; i++) _chart->get(i, &entries[i]);
                xSemaphoreGive(sensorMutex);
            } else {
                Serial.print("$LC,ERR,BUSY\n");
                break;
            }
            snprintf(buf, sizeof(buf), "$LC,BEGIN,%u\n", (unsigned)n);
            Serial.print(buf);
            for (uint8_t i = 0; i < n; i++) {
                snprintf(buf, sizeof(buf), "$LC,E,%u,%.2f,%.1f,%.2f\n",
                         (unsigned)i, entries[i].angle, entries[i].extensionMM, entries[i].limitKg);
                Serial.print(buf);
            }
            Serial.print("$LC,END\n");
            break;
        }
        default:
            Serial.print("$LC,ERR,BAD_COMMAND\n");
            break;
    }
}

/**
 * CAL3: record the boom-angle load correction from a known hanging weight, or
 * clear it. Averages ~0.5 s of readings first, and refuses if the load cell is
 * unhealthy or the boom is moving, so one bad sample can't skew the table.
 */
static void handleLoadGainCal(const ParsedCommand& cmd) {
    char buf[96];

    if (!_comp) {
        Serial.print("$ACK,CAL3,ERR,UNAVAILABLE\n");
        return;
    }

    if (cmd.calClear) {
        LoadCompStatus s = _comp->clear();
        if (s == LCOMP_OK) {
            Serial.print("$ACK,CAL3,CLEARED\n");
        } else {
            snprintf(buf, sizeof(buf), "$ACK,CAL3,ERR,%s\n", LoadCompensation::statusName(s));
            Serial.print(buf);
        }
        return;
    }

    const int SAMPLES = 20;
    float sumLoad = 0.0f, sumAngle = 0.0f;
    float minAngle = 1e9f, maxAngle = -1e9f;
    for (int i = 0; i < SAMPLES; i++) {
        float load = 0.0f, angle = 0.0f;
        bool  ok = false;
        if (xSemaphoreTake(sensorMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            load  = sensorData.loadCellRaw;
            angle = sensorData.boomAngle;
            ok    = sensorData.loadSensorOK;
            xSemaphoreGive(sensorMutex);
        }
        if (!ok) {
            Serial.print("$ACK,CAL3,ERR,LOAD_SENSOR_FAULT\n");
            return;
        }
        sumLoad  += load;
        sumAngle += angle;
        if (angle < minAngle) minAngle = angle;
        if (angle > maxAngle) maxAngle = angle;
        vTaskDelay(pdMS_TO_TICKS(25));
    }

    if (maxAngle - minAngle > 1.0f) {
        Serial.print("$ACK,CAL3,ERR,BOOM_MOVING\n");
        return;
    }

    const float angle = sumAngle / SAMPLES;
    const float load  = sumLoad / SAMPLES;
    LoadCompStatus s = _comp->record(angle, load, cmd.calWeight);
    if (s == LCOMP_OK) {
        snprintf(buf, sizeof(buf), "$ACK,CAL3,OK,ANGLE:%.1f,GAIN:%.4f\n", angle, _comp->gainAt(angle));
    } else {
        snprintf(buf, sizeof(buf), "$ACK,CAL3,ERR,%s\n", LoadCompensation::statusName(s));
    }
    Serial.print(buf);
}

void commandTask_init(
    MegaBridge* mega,
    N20Encoder* winch,
    HX711Driver* loadCell,
    MPU6050Driver* imu,
    AS5600Driver* teleEnc,
    AS5600Driver* swingEnc,
    AS5600Driver* boomEnc,
    LoadChart* loadChart,
    LoadCompensation* loadComp
) {
    _mega     = mega;
    _winch    = winch;
    _loadCell = loadCell;
    _imu      = imu;
    _teleEnc  = teleEnc;
    _swingEnc = swingEnc;
    _boomEnc  = boomEnc;
    _chart    = loadChart;
    _comp     = loadComp;
}

void commandTask(void* pvParameters) {
    GCodeParser parser;
    char lineBuffer[128];
    int lineIdx = 0;

    for (;;) {
        // Read characters from USB Serial until we get a full line
        while (Serial.available()) {
            char c = Serial.read();

            if (c == '\n' || c == '\r') {
                if (lineIdx == 0) continue;  // Skip empty lines

                lineBuffer[lineIdx] = '\0';  // Null-terminate
                lineIdx = 0;

                // Parse the G-code line
                ParsedCommand cmd;
                ParseResult result = parser.parse(lineBuffer, &cmd);

                if (result != PARSE_OK) continue;

                // Dispatch based on command type
                switch (cmd.type) {

                    case CMD_ESTOP: {
                        // Latch first: from here on no motion command is accepted until RST
                        g_estopLatched = true;
                        // Emergency stop ALL motors
                        if (_mega)  _mega->sendMotorCommand(cmd.motor);
                        if (_winch) _winch->stopMotor();
                        _winchActive = false;
                        // Update alarm level
                        if (xSemaphoreTake(sensorMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                            sensorData.alarmLevel = 3;  // ESTOP
                            xSemaphoreGive(sensorMutex);
                        }
                        Serial.println("$ACK,ESTOP");
                        break;
                    }

                    case CMD_RESET: {
                        // Clear the latch, then let the Mega re-enable its drivers
                        g_estopLatched   = false;
                        _blockedReported = false;
                        if (_mega) _mega->sendReset();
                        Serial.println("$ACK,RST,ESTOP_CLEARED");
                        break;
                    }

                    case CMD_MOTOR: {
                        if (g_estopLatched) {
                            if (!_blockedReported) {
                                Serial.println("$ACK,BLOCKED,ESTOP_LATCHED");
                                _blockedReported = true;
                            }
                            break;
                        }
                        if (cmd.motor.axis == 4) {
                            // Winch — handled directly by ESP32 DRV8833
                            if (_winch) {
                                uint8_t pwm = speedToPwm(cmd.motor.speed);
                                _winch->setMotor(pwm, cmd.motor.direction);
                                _winchActive    = (pwm > 0);
                                _lastWinchCmdMs = millis();
                            }
                        } else {
                            // Axes 1-3 (Swing, Lift, Tele) → forward to Mega
                            if (_mega) _mega->sendMotorCommand(cmd.motor);
                        }
                        break;
                    }

                    case CMD_STOP_AXIS: {
                        if (cmd.motor.stopAxis == 4) {
                            // Stop winch locally
                            if (_winch) _winch->stopMotor();
                            _winchActive = false;
                        } else {
                            // Stop stepper axis on Mega
                            if (_mega) _mega->sendMotorCommand(cmd.motor);
                        }
                        break;
                    }

                    case CMD_CALIBRATE: {
                        switch (cmd.calType) {
                            case CAL_TARE_LOAD:
                                if (_loadCell) {
                                    _loadCell->tare(10);
                                    Serial.println("$ACK,CAL0,TARE_DONE");
                                }
                                break;
                            case CAL_ZERO_IMU:
                                sensorTask_requestIMUCalibration(
                                    (uint8_t)cmd.boomAxis,
                                    (cmd.boomInv != 0),
                                    (uint8_t)cmd.tiltAxis,
                                    (cmd.tiltInv != 0)
                                );
                                Serial.println("$ACK,CAL1,IMU_ZEROED");
                                break;
                            case CAL_RESET_TELE:
                                sensorTask_resetTelescope(cmd.teleScale, cmd.teleInvert);
                                break;
                            case CAL_LOAD_GAIN:
                                handleLoadGainCal(cmd);
                                break;
                        }
                        break;
                    }

                    case CMD_DEBUG: {
                        // Send I2C scan + sensor status report
                        TelemetryFormatter fmt;
                        float boomCheckErr = 0.0f;
                        uint8_t boomCheckState = sensorTask_getBoomCheck(&boomCheckErr);
                        char dbgBuf[768];
                        uint16_t fsrVals[4] = {0};
                        GainPoint gains[LCOMP_MAX_POINTS];
                        uint8_t gainCount = 0;
                        if (xSemaphoreTake(sensorMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                            memcpy(fsrVals, sensorData.fsr, sizeof(fsrVals));
                            if (_comp) {
                                gainCount = _comp->count();
                                for (uint8_t i = 0; i < gainCount; i++) _comp->get(i, &gains[i]);
                            }
                            xSemaphoreGive(sensorMutex);
                        }
                        fmt.formatDebugReport(
                            _swingEnc ? _swingEnc->isConnected() : false,
                            _boomEnc  ? _boomEnc->isConnected()  : false,
                            _teleEnc  ? _teleEnc->isConnected()  : false,
                            _imu      ? _imu->isConnected()      : false,
                            _loadCell ? _loadCell->isConnected()  : false,
                            fsrVals,
                            _winch ? _winch->getCount() : 0,
                            sensorTask_getTeleScale(), sensorTask_getTeleInvert(),
                            gains, gainCount,
                            boomCheckState, boomCheckErr,
                            dbgBuf, sizeof(dbgBuf)
                        );
                        Serial.print(dbgBuf);
                        break;
                    }

                    case CMD_LOADCHART: {
                        handleLoadChart(cmd);
                        break;
                    }

                    case CMD_CONFIG: {
                        telemetryTask_setRate(cmd.configRate);
                        Serial.print("$ACK,CFG,RATE,");
                        Serial.println(cmd.configRate);
                        break;
                    }

                    default:
                        break;
                }
            } else {
                // Buffer the character (prevent overflow)
                if (lineIdx < (int)(sizeof(lineBuffer) - 1)) {
                    lineBuffer[lineIdx++] = c;
                }
            }
        }

        // Dead-man watchdog for the winch: stop it if the command stream goes quiet
        if (_winchActive && (millis() - _lastWinchCmdMs) > MOTION_TIMEOUT_MS) {
            if (_winch) _winch->stopMotor();
            _winchActive = false;
            Serial.println("$ACK,WATCHDOG,WINCH_STOPPED");
        }

        // Yield to other tasks when no serial data
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
