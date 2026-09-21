// ============================================================
//  N20 DC Motor Quadrature Encoder Driver
//  Uses hardware interrupts for accurate pulse counting.
//  Also provides DRV8833 motor control for the winch.
// ============================================================
#ifndef N20_ENCODER_H
#define N20_ENCODER_H

#include "config.h"

/**
 * Quadrature encoder driver for the N20 winch motor.
 *
 * How it works:
 *   - Encoder channels A and B produce square waves 90° out of phase
 *   - On each rising edge of channel A, we check channel B:
 *     - B is HIGH → rotating forward  → count++
 *     - B is LOW  → rotating backward → count--
 *   - This gives us direction + position from a single interrupt
 *
 * Also provides DRV8833 motor control since the winch is driven
 * directly by the ESP32 (not through RAMPS/Mega).
 */
class N20Encoder {
public:
    N20Encoder()
        : _count(0), _ropeLengthMM(0.0f) {}

    /**
     * Initialize encoder interrupt and motor driver pins.
     * Call in setup() BEFORE creating FreeRTOS tasks.
     */
    void begin();

    /** Get current encoder tick count (atomic read). */
    long getCount();

    /**
     * Compute rope length in mm from encoder ticks.
     * Uses ROPE_MM_PER_TICK from config.h.
     */
    float getRopeLengthMM();

    // --- DRV8833 Motor Control ---

    /**
     * Set winch motor speed and direction.
     * @param speed  PWM value 0–255
     * @param dir    0 = wind up, 1 = pay out
     */
    void setMotor(uint8_t speed, uint8_t dir);

    /** Stop the winch motor immediately. */
    void stopMotor();

    // ISR must be static — accesses _encoderCount via global pointer
    static void IRAM_ATTR encoderISR();

private:
    volatile long _count;  // Accessed from ISR — must be volatile
    float  _ropeLengthMM;
};

// Global pointer for ISR access (ESP32 ISRs must be static/global)
extern N20Encoder* g_n20Instance;

#endif // N20_ENCODER_H
