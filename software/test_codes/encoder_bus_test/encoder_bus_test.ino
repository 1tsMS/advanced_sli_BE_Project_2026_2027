// ============================================================
//  Encoder bus connection test (new protoboard)
//
//  Checks the three AS5600 buses with the same bit-banged SoftI2C
//  the main firmware (esp32_mainv2) uses:
//    bus 3  GPIO15 SDA / GPIO5  SCL  -> swing AS5600     (new bus, J2)
//    bus 1  GPIO25 SDA / GPIO26 SCL  -> boom AS5600      (J3)
//    bus 2  GPIO32 SDA / GPIO33 SCL  -> telescope AS5600 (J4)
//
//  At boot, per bus:
//    1. pull-up check: each line is read with only the ESP32's weak
//       internal pull-down. HIGH = an external pull-up is there.
//    2. address probe for 0x36.
//  Then every encoder is read at 100 Hz, 3 reads per cycle (like HwBusTask),
//  and a status block is printed twice a second: angle, fail rate,
//  magnet health. Turn each axis by hand: only its own line should move.
//
//  Serial 115200. Commands: r = reset counters, p = redo the boot checks.
//  soft_i2c.h is a copy of firmware/esp32_mainv2/soft_i2c.h: keep them identical.
// ============================================================
#include "soft_i2c.h"

#define AS5600_ADDR     0x36
#define REG_STATUS      0x0B   // MD (bit 5), ML (bit 4), MH (bit 3)
#define REG_RAW_ANGLE   0x0C
#define REG_AGC         0x1A
#define REG_MAGNITUDE   0x1B

#define READ_PERIOD_MS   10    // 100 Hz, same rate as HwBusTask
#define READS_PER_CYCLE  3     // the driver takes the median of 3
#define PRINT_PERIOD_MS  500
#define PASS_FAIL_PCT    1.0f  // more failed reads than this = wiring problem

struct EncBus {
    EncBus(const char* n, uint8_t busNo, uint8_t sdaPin, uint8_t sclPin, uint32_t delayUs)
        : name(n), bus(busNo), sda(sdaPin), scl(sclPin), i2c(sdaPin, sclPin, delayUs) {}

    const char* name;
    uint8_t     bus, sda, scl;
    SoftI2C     i2c;
    bool        acked   = false;
    uint32_t    reads   = 0;
    uint32_t    fails   = 0;
    uint16_t    lastRaw = 0;
    bool        hasRaw  = false;
};

// Half-bit delays match esp32_mainv2.ino
EncBus buses[] = {
    EncBus("SWING", 3, 15, 5,  5),
    EncBus("BOOM",  1, 25, 26, 8),
    EncBus("TELE",  2, 32, 33, 5),
};
const uint8_t BUS_COUNT = sizeof(buses) / sizeof(buses[0]);

// ---------------------------------------------------------------- boot checks

/** Read one pin as a plain input with only the internal pull-down or pull-up on. */
static int lineLevel(uint8_t pin, bool pullUp) {
    gpio_config_t c = {};
    c.pin_bit_mask = 1ULL << pin;
    c.mode         = GPIO_MODE_INPUT;
    c.pull_up_en   = pullUp ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    c.pull_down_en = pullUp ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE;
    c.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&c);
    delayMicroseconds(200);   // ~45k internal x cable capacitance settles in << 200 us
    return gpio_get_level((gpio_num_t)pin);
}

/**
 * External pull-up (4.7k, or the module's own) beats the ~45k internal pull-down,
 * so the line reads HIGH. If it reads LOW even with the internal pull-up, something
 * is holding it down: a short to GND, or an unpowered module clamping it.
 */
static const char* lineVerdict(uint8_t pin) {
    if (lineLevel(pin, false)) return "pull-up OK";
    if (lineLevel(pin, true))  return "NO external pull-up";
    return "STUCK LOW (short to GND, or module has no power)";
}

static void checkBus(EncBus& b) {
    Serial.printf("[%-5s] bus %u  SDA GPIO%-2u: %s\n", b.name, b.bus, b.sda, lineVerdict(b.sda));
    Serial.printf("[%-5s] bus %u  SCL GPIO%-2u: %s\n", b.name, b.bus, b.scl, lineVerdict(b.scl));

    b.i2c.begin();   // back to open-drain + bus recovery
    b.acked = b.i2c.beginTransmission(AS5600_ADDR);
    b.i2c.endTransmission();
    if (b.acked) {
        Serial.printf("[%-5s] AS5600 ACK at 0x36\n", b.name);
    } else {
        Serial.printf("[%-5s] NO ACK: check VCC/GND, SDA/SCL swapped, plug in the right JST\n", b.name);
    }
}

static void bootChecks() {
    Serial.println("\n=== Encoder bus check ===");
    for (uint8_t i = 0; i < BUS_COUNT; i++) checkBus(buses[i]);
    Serial.println("Turn each axis by hand: only its own line should change.\n");
}

// ---------------------------------------------------------------- reads

/** One raw-angle read, same validity rule as AS5600Driver::readAngleOnce(). */
static bool readAngleOnce(EncBus& b, uint16_t* out) {
    uint8_t buf[2];
    b.reads++;
    // Top nibble of the high byte is always 0; anything else is a bus error
    // (0xFFFF = floating SDA, nobody answered).
    if (!b.i2c.readRegs(AS5600_ADDR, REG_RAW_ANGLE, buf, 2) || (buf[0] & 0xF0)) {
        b.fails++;
        return false;
    }
    *out = ((uint16_t)buf[0] << 8) | buf[1];
    return true;
}

static const char* magnetText(EncBus& b, uint8_t* agc, uint16_t* mag) {
    uint8_t status = 0, m[2] = {0, 0};
    if (!b.i2c.readRegs(AS5600_ADDR, REG_STATUS, &status, 1) ||
        !b.i2c.readRegs(AS5600_ADDR, REG_AGC, agc, 1) ||
        !b.i2c.readRegs(AS5600_ADDR, REG_MAGNITUDE, m, 2)) {
        return "no reply";
    }
    *mag = (((uint16_t)m[0] << 8) | m[1]) & 0x0FFF;
    if (!(status & 0x20)) return "NOT DETECTED";
    if (status & 0x10)    return "TOO WEAK";
    if (status & 0x08)    return "TOO STRONG";
    return "OK";
}

static void printStatus() {
    Serial.printf("--- t=%.1f s ---\n", millis() / 1000.0f);
    for (uint8_t i = 0; i < BUS_COUNT; i++) {
        EncBus& b = buses[i];
        uint8_t  agc = 0;
        uint16_t mag = 0;
        const char* magnet = magnetText(b, &agc, &mag);
        const float failPct = b.reads ? 100.0f * b.fails / b.reads : 100.0f;
        const bool  pass = b.hasRaw && failPct < PASS_FAIL_PCT && strcmp(magnet, "OK") == 0;

        Serial.printf("%-5s b%u | ", b.name, b.bus);
        if (b.hasRaw) {
            Serial.printf("raw %4u %6.1f deg | ", b.lastRaw, b.lastRaw * 360.0f / 4096.0f);
        } else {
            Serial.print("raw  ---    --- deg | ");
        }
        Serial.printf("fail %lu/%lu (%.2f%%) | magnet %-12s AGC %3u MAG %4u | sclFault %lu | %s\n",
                      (unsigned long)b.fails, (unsigned long)b.reads, failPct,
                      magnet, agc, mag, (unsigned long)b.i2c.sclFaults(),
                      pass ? "PASS" : "CHECK");
    }
}

static void resetCounters() {
    for (uint8_t i = 0; i < BUS_COUNT; i++) {
        buses[i].reads = buses[i].fails = 0;
        buses[i].hasRaw = false;
    }
    Serial.println("Counters reset.");
}

// ---------------------------------------------------------------- main

void setup() {
    Serial.begin(115200);
    delay(500);
    bootChecks();
}

void loop() {
    static uint32_t lastRead = 0, lastPrint = 0;
    const uint32_t now = millis();

    if (now - lastRead >= READ_PERIOD_MS) {
        lastRead = now;
        for (uint8_t i = 0; i < BUS_COUNT; i++) {
            uint16_t raw;
            for (uint8_t k = 0; k < READS_PER_CYCLE; k++) {
                if (readAngleOnce(buses[i], &raw)) {
                    buses[i].lastRaw = raw;
                    buses[i].hasRaw  = true;
                }
            }
        }
    }

    if (now - lastPrint >= PRINT_PERIOD_MS) {
        lastPrint = now;
        printStatus();
    }

    if (Serial.available()) {
        const char c = Serial.read();
        if (c == 'r') resetCounters();
        if (c == 'p') { bootChecks(); resetCounters(); }
    }
}
