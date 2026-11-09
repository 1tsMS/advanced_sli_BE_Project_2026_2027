// ============================================================
//  Mega Bridge — UART2 link to the Arduino Mega (RAMPS 1.4)
//
//  Write-only: CommandTask (the only user) sends stepper commands as text
//  lines; the Mega parses them and drives the A4988s. The winch (M4) runs on
//  the ESP32's own DRV8833, so it is never sent here. Nothing reads the Mega's
//  replies: a lost line is covered by the dead-man (motions repeat every 100 ms)
//  and by the periodic resend of the shaper / driver-idle settings.
// ============================================================
#ifndef MEGA_BRIDGE_H
#define MEGA_BRIDGE_H

#include "config.h"

class MegaBridge {
public:
    /** Open UART2 at MEGA_BAUD on MEGA_RX_PIN / MEGA_TX_PIN. */
    void begin(HardwareSerial* serial);

    /** M1-M3 motion ("M2 S800 D1"), axis stop ("M0 A2") or E-stop ("T0"). */
    void sendMotorCommand(const MotorCommand& cmd);

    /** Any other line (shaper "I1 P389", driver idle "R1", ...). */
    void sendRaw(const char* line);

private:
    HardwareSerial* _serial = nullptr;
};

#endif // MEGA_BRIDGE_H
