// ============================================================
//  Mega Bridge — UART2 Communication to Arduino Mega
//  Forwards motor G-code commands from ESP32 to Mega.
//  The Mega + RAMPS 1.4 handles stepper motor execution.
// ============================================================
#ifndef MEGA_BRIDGE_H
#define MEGA_BRIDGE_H

#include "config.h"

/**
 * Manages the UART2 serial link between ESP32 and Arduino Mega.
 *
 * Data flow:
 *   ESP32 CommandTask receives G-code from PC
 *   → MegaBridge forwards stepper commands (M1-M3, T0) to Mega
 *   → Mega parses and drives steppers via RAMPS 1.4
 *
 * Winch (M4) is handled directly by ESP32 via DRV8833,
 * so M4 commands are NOT forwarded to Mega.
 */
class MegaBridge {
public:
    MegaBridge() : _megaSerial(nullptr), _connected(false) {}

    /**
     * Initialize UART2 connection to Mega.
     * @param serial Pointer to HardwareSerial (Serial2 on ESP32)
     */
    void begin(HardwareSerial* serial);

    /**
     * Send a motor command to the Mega as G-code text.
     * Only forwards M1 (swing), M2 (lift), M3 (tele), T0 (E-stop).
     * M4 (winch) is handled locally by ESP32.
     */
    void sendMotorCommand(const MotorCommand& cmd);

    /** Tell the Mega to clear its E-stop latch and re-enable the stepper drivers. */
    void sendReset();

private:
    HardwareSerial* _megaSerial;
    bool _connected;
};

#endif // MEGA_BRIDGE_H
