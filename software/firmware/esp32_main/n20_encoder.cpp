// ============================================================
//  N20 Encoder + DRV8833 Motor — Implementation
// ============================================================
#include "n20_encoder.h"

// Global instance pointer for ISR access
N20Encoder* g_n20Instance = nullptr;

void N20Encoder::begin() {
    // Store global reference for ISR
    g_n20Instance = this;

    // Encoder pins as inputs with pullup
    pinMode(N20_ENC_A, INPUT_PULLUP);
    pinMode(N20_ENC_B, INPUT_PULLUP);

    // Attach interrupt on channel A rising edge
    attachInterrupt(digitalPinToInterrupt(N20_ENC_A), encoderISR, RISING);

    // DRV8833 motor driver pins
    pinMode(DRV_AIN1, OUTPUT);
    pinMode(DRV_AIN2, OUTPUT);
    pinMode(DRV_STBY, OUTPUT);

    // Enable driver (STBY HIGH = active)
    digitalWrite(DRV_STBY, HIGH);

    // Motor off initially
    analogWrite(DRV_AIN1, 0);
    analogWrite(DRV_AIN2, 0);

    _count = 0;
    _ropeLengthMM = 0.0f;
}

long N20Encoder::getCount() {
    // Atomic read of volatile variable
    noInterrupts();
    long c = _count;
    interrupts();
    return c;
}

float N20Encoder::getRopeLengthMM() {
    long count = getCount();
    _ropeLengthMM = (float)count * ROPE_MM_PER_TICK;
    return _ropeLengthMM;
}

void N20Encoder::setMotor(uint8_t speed, uint8_t dir) {
    if (dir == 0) {
        // Wind up: AIN1 = PWM, AIN2 = LOW
        analogWrite(DRV_AIN1, speed);
        analogWrite(DRV_AIN2, 0);
    } else {
        // Pay out: AIN1 = LOW, AIN2 = PWM
        analogWrite(DRV_AIN1, 0);
        analogWrite(DRV_AIN2, speed);
    }
}

void N20Encoder::stopMotor() {
    analogWrite(DRV_AIN1, 0);
    analogWrite(DRV_AIN2, 0);
}

// --- Interrupt Service Routine ---
// IRAM_ATTR = keep in RAM for fast execution (required for ESP32 ISRs)
void IRAM_ATTR N20Encoder::encoderISR() {
    if (!g_n20Instance) return;

    // On rising edge of A, check B for direction
    if (digitalRead(N20_ENC_B)) {
        g_n20Instance->_count++;   // Forward
    } else {
        g_n20Instance->_count--;   // Backward
    }
}
