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

// ======================== MOTOR STATE ========================
struct StepperState {
    uint8_t stepPin;
    uint8_t dirPin;
    uint8_t enPin;
    float   accel;         // ramp-up rate in steps/s^2; 0 = no ramp (jump to target)
    bool    running;
    uint8_t direction;     // direction currently on the DIR pin
    float   curSps;        // speed right now (steps/s)
    float   targetSps;     // speed last commanded
    uint32_t speed;        // Delay between steps in microseconds (uint16 overflowed below ~16 steps/s)
    unsigned long lastStepTime;
    unsigned long lastCmdMs;   // millis() of the last M-command (dead-man watchdog)
};

// Dead-man: an axis keeps stepping only while its M-command keeps arriving
// (the dashboard repeats it every 100 ms while held). Same value as the ESP32.
#define MOTION_TIMEOUT_MS  400

// Ramp-up only. A stepper starting straight at 1000 steps/s under load stalls
// far below its rated torque; starting at START_SPS and ramping up lets it
// reach speeds it could never jump to. Stops are always instant (release,
// M0, T0, dead-man). Tune LIFT accel on the loaded crane: raise it until the
// lift skips at start, then back off ~30%.
#define START_SPS     100.0f   // speed a ramped axis starts at (below pull-in)
#define RAMP_TICK_US  1000UL   // ramp update period

StepperState motors[3] = {
    // pins                          accel (steps/s^2)
    {SWING_STEP, SWING_DIR, SWING_EN,     0.0f, false, 0, 0, 0, 1000, 0, 0},  // M1 = Swing (no ramp: input shaping on ESP32 later)
    {LIFT_STEP,  LIFT_DIR,  LIFT_EN,   1500.0f, false, 0, 0, 0, 1000, 0, 0},  // M2 = Lift   <- TUNE HERE
    {TELE_STEP,  TELE_DIR,  TELE_EN,      0.0f, false, 0, 0, 0, 1000, 0, 0}   // M3 = Telescope (no ramp)
};
unsigned long lastRampUs = 0;

// ======================== SERIAL BUFFERS ========================
// One line buffer per port: with a shared buffer, a byte arriving on USB
// in the middle of an ESP32 command would be spliced into that command.
struct LineBuffer {
    char buf[64];
    int  len;
};
LineBuffer espLine = { {0}, 0 };
LineBuffer usbLine = { {0}, 0 };

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

// ======================== SERIAL INPUT ========================
void pollSerial(HardwareSerial& port, LineBuffer& line) {
    while (port.available()) {
        char c = port.read();
        if (c == '\n' || c == '\r') {
            if (line.len > 0) {
                line.buf[line.len] = '\0';
                processCommand(line.buf);
                line.len = 0;
            }
        } else if (line.len < (int)sizeof(line.buf) - 1) {
            line.buf[line.len++] = c;
        }
    }
}

// ======================== MAIN LOOP ========================
void loop() {
    // 1. Commands from the ESP32 (UART1), and from USB for direct debugging
    pollSerial(Serial1, espLine);
    pollSerial(Serial, usbLine);

    // 2. Dead-man watchdog: stop any axis whose command stream went quiet
    unsigned long nowMs = millis();
    for (int i = 0; i < 3; i++) {
        if (motors[i].running && (nowMs - motors[i].lastCmdMs) > MOTION_TIMEOUT_MS) {
            hardStop(motors[i]);
            Serial.print("WATCHDOG stopped axis "); Serial.println(i + 1);
        }
    }

    // 3. Ramp speeds toward their targets
    unsigned long now = micros();
    if (now - lastRampUs >= RAMP_TICK_US) {
        const float dt = (now - lastRampUs) / 1000000.0f;
        lastRampUs = now;
        for (int i = 0; i < 3; i++) updateRamp(motors[i], dt);
    }

    // 4. Step any running motors using non-blocking timing
    now = micros();
    for (int i = 0; i < 3; i++) {
        if (motors[i].running && (now - motors[i].lastStepTime >= motors[i].speed)) {
            digitalWrite(motors[i].stepPin, HIGH);
            delayMicroseconds(2);  // Minimum pulse width for A4988
            digitalWrite(motors[i].stepPin, LOW);
            // Schedule from the ideal time, not "now", so loop jitter doesn't
            // slow the axis down; resync if we fell more than one step behind.
            motors[i].lastStepTime += motors[i].speed;
            if (now - motors[i].lastStepTime >= motors[i].speed) motors[i].lastStepTime = now;
        }
    }
}

// ======================== ACCELERATION RAMP ========================
// Raises curSps toward targetSps by accel*dt per tick. Lower targets
// (slider moved down) are applied at once.
void updateRamp(StepperState& m, float dt) {
    if (!m.running || m.curSps == m.targetSps) return;

    if (m.curSps < m.targetSps) m.curSps = min(m.targetSps, m.curSps + m.accel * dt);
    else                        m.curSps = m.targetSps;

    m.speed = (uint32_t)(1000000.0f / m.curSps);
}

void hardStop(StepperState& m) {
    m.running   = false;
    m.curSps    = 0.0f;
    m.targetSps = 0.0f;
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

    // --- M0 Ax: Stop specific axis ---
    if (cmd[0] == 'M' && cmd[1] == '0') {
        int axis = extractParam(cmd, 'A', 0);
        if (axis >= 1 && axis <= 3) {
            hardStop(motors[axis - 1]);
            Serial.print("Stopped axis "); Serial.println(axis);
        }
        return;
    }

    // --- M1-M3: Motor commands ---
    if (cmd[0] == 'M' && cmd[1] >= '1' && cmd[1] <= '3') {
        int axis = cmd[1] - '0';       // 1, 2, or 3
        int speed = extractParam(cmd, 'S', 0);
        int dir   = extractParam(cmd, 'D', 0);

        int idx = axis - 1;  // Array index (0-based)

        // S is the target speed in steps/s; a ramped axis gets there at its
        // accel, the others jump straight to it.
        StepperState& m = motors[idx];
        if (speed <= 0) {
            hardStop(m);
            return;
        }

        const uint8_t newDir = dir ? 1 : 0;
        // A direction change while moving restarts the axis from standstill
        const bool wasRunning = m.running && (m.direction == newDir);
        m.targetSps = (float)speed;
        m.lastCmdMs = millis();   // feed the dead-man watchdog

        if (!wasRunning) {
            // DIR first, then begin at a speed the motor can pull in
            m.direction = newDir;
            digitalWrite(m.dirPin, m.direction ? HIGH : LOW);
            m.curSps = (m.accel > 0.0f) ? min(m.targetSps, START_SPS) : m.targetSps;
            m.speed  = (uint32_t)(1000000.0f / m.curSps);
            m.running = true;
            m.lastStepTime = micros();
        }

        if (!wasRunning) {   // repeats arrive every 100 ms: only log starts
            Serial.print("M"); Serial.print(axis);
            Serial.print(" S"); Serial.print(speed);
            Serial.print(" D"); Serial.println(dir);
        }
    }
}

// ======================== EMERGENCY STOP ========================
void emergencyStop() {
    for (int i = 0; i < 3; i++) {
        hardStop(motors[i]);   // no ramp: stop now
        // Optionally disable drivers for immediate coast-down:
        // digitalWrite(motors[i].enPin, HIGH);  // HIGH = disabled
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

