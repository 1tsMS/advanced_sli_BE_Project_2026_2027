// ============================================================
//  N20 Winch: quadrature encoder (ESP32 PCNT) + DRV8833 motor control
//
//  The encoder is counted by the ESP32's hardware pulse counter (PCNT)
//  in 4x quadrature mode with its glitch filter on. The previous version
//  used one GPIO interrupt on channel A's rising edge (1x decoding, no
//  filtering): PWM noise from the motor wires running next to the encoder
//  wires produced false counts, and edges could be missed under load.
//  PCNT counts in hardware, so nothing depends on interrupt latency.
//
//  PCNT has a 16-bit counter. poll() (called every SensorTask cycle) folds
//  it into a 32-bit count; between two polls the motor turns far less than
//  the counter range, so no wrap can be missed.
// ============================================================
#ifndef N20_ENCODER_H
#define N20_ENCODER_H

#include "config.h"

class N20Encoder {
public:
    /** Configure PCNT on N20_ENC_A/B and the DRV8833 pins. Call in setup(). */
    void begin();

    /** Fold the hardware counter into the 32-bit count. Call every SensorTask cycle. */
    void poll();

    /** Quadrature counts (4 per encoder line) since boot. Safe to read from any task. */
    int32_t getCount() const { return _count; }

    // --- DRV8833 Motor Control ---
    /**
     * @param speed  PWM 0-255 (0 = brake)
     * @param dir    0 = wind up, 1 = pay out
     */
    void setMotor(uint8_t speed, uint8_t dir);

    /** Stop the winch and hold it: both inputs HIGH = DRV8833 brake (slow decay).
     *  Coasting (both LOW) would let a hanging load unwind the drum. */
    void stopMotor();

private:
    volatile int32_t _count = 0;
    int16_t          _lastHw = 0;
    bool             _ready = false;
};

#endif // N20_ENCODER_H
