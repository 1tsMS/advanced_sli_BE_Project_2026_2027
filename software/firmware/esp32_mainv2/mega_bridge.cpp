// ============================================================
//  Mega Bridge — Implementation
// ============================================================
#include "mega_bridge.h"
#include <stdio.h>

void MegaBridge::begin(HardwareSerial* serial) {
    _serial = serial;
    _serial->begin(MEGA_BAUD, SERIAL_8N1, MEGA_RX_PIN, MEGA_TX_PIN);
}

void MegaBridge::sendMotorCommand(const MotorCommand& cmd) {
    if (!_serial) return;
    char line[24];
    if (cmd.isEstop) {
        _serial->println("T0");
    } else if (cmd.axis >= 1 && cmd.axis <= 3) {
        snprintf(line, sizeof(line), "M%u S%u D%u", cmd.axis, cmd.speed, cmd.direction);
        _serial->println(line);
    } else if (cmd.axis == 0 && cmd.stopAxis >= 1 && cmd.stopAxis <= 3) {
        snprintf(line, sizeof(line), "M0 A%u", cmd.stopAxis);
        _serial->println(line);
    }
}

void MegaBridge::sendRaw(const char* line) {
    if (_serial) _serial->println(line);
}
