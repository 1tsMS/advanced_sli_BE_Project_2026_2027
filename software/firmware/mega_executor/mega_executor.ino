// ============================================================
//  Arduino Mega — Motor Executor Firmware
//  Receives G-code commands from ESP32 via Serial1 (UART)
//  and controls 3 stepper motors via RAMPS 1.4 + A4988 drivers.
//
//  This is a "dumb" motor driver — all safety logic lives on ESP32.
//  The Mega just translates G-code into step/dir pin pulses.
//
//  RAMPS 1.4 Pin Mapping:
//    X-axis (Swing):     STEP=54, DIR=55, EN=38
//    Y-axis (Telescope):  STEP=60, DIR=61, EN=56
//    Z-axis (Boom Lift): STEP=46, DIR=48, EN=62
// ============================================================

#include <Arduino.h>
#include <ctype.h>

// ======================== RAMPS 1.4 PIN DEFINITIONS ========================
// X-axis: Swing
#define SWING_STEP  54
#define SWING_DIR   55
#define SWING_EN    38

// Y-axis: Telescope (M3)
#define TELE_STEP   60
#define TELE_DIR    61
#define TELE_EN     56

// Z-axis: Boom Lift (M2)
#define LIFT_STEP   46
#define LIFT_DIR    48
#define LIFT_EN     62

// ======================== SAFETY SETTINGS ========================
// Dead-man timeout: an axis keeps stepping only while its M-command keeps being
// repeated by the ESP32. Must match MOTION_TIMEOUT_MS in the ESP32 config.h.
#define MOTION_TIMEOUT_MS  400

// 1 = E-stop also disables the stepper drivers (no holding torque, so a loaded
//     boom relies on the gearing to hold). 0 = E-stop only stops stepping and
//     the drivers keep holding position.
#define ESTOP_DISABLES_DRIVERS  1

// ======================== MOTOR STATE ========================
struct StepperState {
    uint8_t stepPin;
    uint8_t dirPin;
    uint8_t enPin;
    bool    running;
    uint8_t direction;
    uint16_t speed;        // Delay between steps in microseconds
    unsigned long lastStepTime;
    unsigned long lastCmdMs;   // millis() of the last M-command for this axis (watchdog)
};

StepperState motors[3] = {
    {SWING_STEP, SWING_DIR, SWING_EN, false, 0, 1000, 0, 0},  // M1 = Swing
    {LIFT_STEP,  LIFT_DIR,  LIFT_EN,  false, 0, 1000, 0, 0},   // M2 = Lift
    {TELE_STEP,  TELE_DIR,  TELE_EN,  false, 0, 1000, 0, 0}    // M3 = Telescope
};

// Set by T0, cleared by RST. While set, M1-M3 commands are ignored.
bool estopped = false;

// ======================== SERIAL BUFFER ========================
char cmdBuffer[64];
int cmdIndex = 0;

// ======================== SETUP ========================
void setup() {
    Serial.begin(115200);   // USB (for debug, optional)
    Serial1.begin(9600);    // UART from ESP32 (Pin 18=TX1, Pin 19=RX1)

    // Configure stepper pins
    for (int i = 0; i < 3; i++) {
        pinMode(motors[i].stepPin, OUTPUT);
        pinMode(motors[i].dirPin,  OUTPUT);
        pinMode(motors[i].enPin,   OUTPUT);
        digitalWrite(motors[i].enPin, LOW);  // Enable drivers (active LOW)
    }

    Serial.println("Mega Executor Ready");
    Serial1.println("$MEGA,READY");
}

// ======================== MAIN LOOP ========================
void loop() {
    // 1. Check for incoming commands from ESP32
    while (Serial1.available()) {
        char c = Serial1.read();
        if (c == '\n' || c == '\r') {
            if (cmdIndex > 0) {
                cmdBuffer[cmdIndex] = '\0';
                processCommand(cmdBuffer);
                cmdIndex = 0;
            }
        } else if (cmdIndex < (int)sizeof(cmdBuffer) - 1) {
            cmdBuffer[cmdIndex++] = c;
        }
    }

    // Also accept commands from USB Serial (for direct debugging)
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (cmdIndex > 0) {
                cmdBuffer[cmdIndex] = '\0';
                processCommand(cmdBuffer);
                cmdIndex = 0;
            }
        } else if (cmdIndex < (int)sizeof(cmdBuffer) - 1) {
            cmdBuffer[cmdIndex++] = c;
        }
    }

    // 2. Dead-man watchdog: stop any axis whose command stream has gone quiet
    unsigned long nowMs = millis();
    for (int i = 0; i < 3; i++) {
        if (motors[i].running && (nowMs - motors[i].lastCmdMs) > MOTION_TIMEOUT_MS) {
            motors[i].running = false;
            Serial.print("WATCHDOG stopped axis "); Serial.println(i + 1);
        }
    }

    // 3. Step any running motors using non-blocking timing
    unsigned long now = micros();
    for (int i = 0; i < 3; i++) {
        if (motors[i].running && (now - motors[i].lastStepTime >= motors[i].speed)) {
            digitalWrite(motors[i].stepPin, HIGH);
            delayMicroseconds(2);  // Minimum pulse width for A4988
            digitalWrite(motors[i].stepPin, LOW);
            motors[i].lastStepTime = now;
        }
    }
}

// ======================== COMMAND PARSER ========================
void processCommand(const char* cmd) {
    // --- T0: Emergency Stop ---
    if (cmd[0] == 'T' && cmd[1] == '0') {
        emergencyStop();
        Serial.println("E-STOP TRIGGERED");
        Serial1.println("$ACK,ESTOP");
        return;
    }

    // --- RST: Clear E-stop and re-enable drivers ---
    if (cmd[0] == 'R' && cmd[1] == 'S' && cmd[2] == 'T') {
        estopped = false;
        for (int i = 0; i < 3; i++) {
            digitalWrite(motors[i].enPin, LOW);  // Enable drivers (active LOW)
        }
        Serial.println("E-STOP CLEARED");
        Serial1.println("$ACK,RST");
        return;
    }

    // --- M0 Ax: Stop specific axis ---
    if (cmd[0] == 'M' && cmd[1] == '0') {
        int axis = extractParam(cmd, 'A', 0);
        if (axis >= 1 && axis <= 3) {
            motors[axis - 1].running = false;
            Serial.print("Stopped axis "); Serial.println(axis);
        }
        return;
    }

    // --- M1-M3: Motor commands ---
    if (cmd[0] == 'M' && cmd[1] >= '1' && cmd[1] <= '3') {
        if (estopped) return;          // Latched E-stop: ignore motion until RST

        int axis = cmd[1] - '0';       // 1, 2, or 3
        int speed = extractParam(cmd, 'S', 0);
        int dir   = extractParam(cmd, 'D', 0);

        int idx = axis - 1;  // Array index (0-based)

        // Convert speed (steps/sec) to step delay in microseconds
        // e.g. Speed 500 = 2000µs delay (matches RAMPS_test.ino)
        if (speed == 0) {
            motors[idx].running = false;
            return;
        }

        motors[idx].speed = 1000000UL / speed;
        motors[idx].direction = dir;
        motors[idx].running = true;
        motors[idx].lastCmdMs = millis();   // Feed the dead-man watchdog

        // Set direction pin
        digitalWrite(motors[idx].dirPin, dir ? HIGH : LOW);

        Serial.print("M"); Serial.print(axis);
        Serial.print(" S"); Serial.print(speed);
        Serial.print(" D"); Serial.println(dir);
    }
}

// ======================== EMERGENCY STOP ========================
void emergencyStop() {
    estopped = true;
    for (int i = 0; i < 3; i++) {
        motors[i].running = false;
#if ESTOP_DISABLES_DRIVERS
        digitalWrite(motors[i].enPin, HIGH);  // HIGH = disabled (active LOW enable)
#endif
    }
}

// ======================== PARAMETER EXTRACTION ========================
int extractParam(const char* line, char prefix, int defaultVal) {
    char target = toupper(prefix);
    const char* p = line;
    while (*p) {
        if (toupper(*p) == target) {
            p++;
            while (*p == ' ') p++;
            if ((*p >= '0' && *p <= '9') || *p == '-') return atoi(p);
        }
        p++;
    }
    return defaultVal;
}

