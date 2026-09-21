#pragma once
#include "Arduino.h"
class TwoWire { public: void setClock(uint32_t) {} void begin(...) {} void setTimeOut(uint16_t) {} };
extern TwoWire Wire;
extern TwoWire Wire1;
