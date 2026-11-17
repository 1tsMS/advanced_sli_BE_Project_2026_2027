// ============================================================
//  Software (bit-banged) I2C master for ESP32
//
//  All three sensor buses use this instead of the hardware Wire peripheral.
//  Why: in this Arduino core (2.0.11 / ESP-IDF 4.4) a hardware I2C transfer
//  that hangs on noise blocks for at least 1 s, whatever Wire.setTimeOut says;
//  on the crane that froze the IMU / swing readings for ~2 s at a time. A
//  bit-banged transfer can't hang: a corrupted transfer simply fails in well
//  under a millisecond and the caller retries next cycle.
//
//  Pins run as true open-drain outputs (ESP-IDF GPIO_MODE_INPUT_OUTPUT_OD):
//  writing 1 releases the line (pull-up makes it high), writing 0 pulls it low.
//  gpio_set_level / gpio_get_level are register accesses, much faster than
//  the pinMode() switching the first version used.
//
//  API mirrors TwoWire: beginTransmission(), write(), endTransmission(),
//  requestFrom(), read(); plus readRegs() / writeReg() helpers.
// ============================================================
#ifndef SOFT_I2C_H
#define SOFT_I2C_H

#include <Arduino.h>
#include "driver/gpio.h"

class SoftI2C {
public:
    /**
     * @param sdaPin  GPIO for SDA
     * @param sclPin  GPIO for SCL
     * @param delayUs half-bit delay: 5 us ~ 80-100 kHz (needs real pull-ups),
     *                8 us ~ 60 kHz (safer with weak pull-ups / long wires)
     */
    SoftI2C(uint8_t sdaPin, uint8_t sclPin, uint32_t delayUs = 8)
        : _sda((gpio_num_t)sdaPin), _scl((gpio_num_t)sclPin), _delay(delayUs) {}

    /** Configure the pins and free a stuck bus. Call in setup(). */
    inline void begin() {
        gpio_config_t c = {};
        c.pin_bit_mask = (1ULL << _sda) | (1ULL << _scl);
        c.mode         = GPIO_MODE_INPUT_OUTPUT_OD;
        c.pull_up_en   = GPIO_PULLUP_ENABLE;     // backup only: external pull-ups do the real work
        c.pull_down_en = GPIO_PULLDOWN_DISABLE;
        c.intr_type    = GPIO_INTR_DISABLE;
        gpio_config(&c);
        gpio_set_level(_sda, 1);
        gpio_set_level(_scl, 1);
        delayMicroseconds(_delay * 2);
        recoverBus();
    }

    /**
     * A slave reset or noise can leave a device holding SDA low mid-byte.
     * Clocking SCL up to 9 times lets it finish, then a STOP frees the bus.
     */
    inline void recoverBus() {
        for (int i = 0; i < 9 && gpio_get_level(_sda) == 0; i++) {
            sclLow();
            sclHigh();
        }
        stop();
    }

    /** Start a write transaction. @return true if the device ACKed its address */
    inline bool beginTransmission(uint8_t addr) {
        _fault = false;
        start();
        return writeByte((uint8_t)(addr << 1));
    }

    inline bool write(uint8_t data) { return writeByte(data); }

    inline void endTransmission() { stop(); }

    /** Read `qty` bytes (max 32) into the internal buffer; retrieve with read(). */
    inline uint8_t requestFrom(uint8_t addr, uint8_t qty) {
        _rxIndex = 0;
        _rxCount = 0;
        _fault   = false;
        if (qty > sizeof(_rxBuf)) qty = sizeof(_rxBuf);
        start();
        if (!writeByte((uint8_t)((addr << 1) | 0x01))) {
            stop();
            return 0;
        }
        for (uint8_t i = 0; i < qty; i++) {
            _rxBuf[i] = readByte(i < qty - 1);   // ACK all but the last byte
            _rxCount++;
        }
        stop();
        if (_fault) _rxCount = 0;                // SCL never rose: bytes are noise
        return _rxCount;
    }

    inline int available() { return _rxCount - _rxIndex; }

    inline uint8_t read() {
        return (_rxIndex < _rxCount) ? _rxBuf[_rxIndex++] : 0xFF;
    }

    /** Read `len` consecutive registers starting at `reg`. */
    inline bool readRegs(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t len) {
        if (!beginTransmission(addr) || !write(reg)) { stop(); return false; }
        stop();
        if (_fault) return false;
        if (requestFrom(addr, len) < len) return false;
        for (uint8_t i = 0; i < len; i++) buf[i] = read();
        return true;
    }

    /** Write one register. */
    inline bool writeReg(uint8_t addr, uint8_t reg, uint8_t val) {
        const bool ok = beginTransmission(addr) && write(reg) && write(val);
        stop();
        return ok && !_fault;
    }

    /** Transfers where SCL was held low after release (shorted / broken / stuck clock line) */
    inline uint32_t sclFaults() const { return _sclFaults; }

private:
    gpio_num_t _sda, _scl;
    uint32_t   _delay;
    uint8_t    _rxBuf[32];
    uint8_t    _rxIndex = 0, _rxCount = 0;
    bool       _fault = false;      // set when SCL failed to rise during this transfer
    uint32_t   _sclFaults = 0;

    inline void sdaHigh() { gpio_set_level(_sda, 1); }
    inline void sdaLow()  { gpio_set_level(_sda, 0); }
    /** Release SCL and check it actually went high. Nothing on these buses
     *  clock-stretches, so a low SCL here means a wiring fault: the transfer
     *  is marked bad instead of returning bits sampled off a dead clock. */
    inline void sclHigh() {
        gpio_set_level(_scl, 1);
        delayMicroseconds(_delay);
        if (gpio_get_level(_scl) == 0 && !_fault) { _fault = true; _sclFaults++; }
    }
    inline void sclLow()  { gpio_set_level(_scl, 0); delayMicroseconds(_delay); }

    /** START: SDA falls while SCL is high */
    inline void start() {
        sdaHigh();
        sclHigh();
        sdaLow();
        delayMicroseconds(_delay);
        sclLow();
    }

    /** STOP: SDA rises while SCL is high */
    inline void stop() {
        sdaLow();
        sclHigh();
        sdaHigh();
        delayMicroseconds(_delay);
    }

    /** Clock out 8 bits MSB-first, then read the ACK bit. */
    inline bool writeByte(uint8_t data) {
        for (int8_t i = 7; i >= 0; i--) {
            if (data & (1 << i)) sdaHigh(); else sdaLow();
            sclHigh();
            sclLow();
        }
        sdaHigh();                                   // release SDA for the ACK
        sclHigh();
        const bool ack = (gpio_get_level(_sda) == 0);
        sclLow();
        return ack && !_fault;
    }

    /** Clock in 8 bits MSB-first, then send ACK (more to read) or NACK (last byte). */
    inline uint8_t readByte(bool ack) {
        uint8_t data = 0;
        sdaHigh();                                   // release SDA so the slave can drive it
        for (int8_t i = 7; i >= 0; i--) {
            sclHigh();
            if (gpio_get_level(_sda)) data |= (uint8_t)(1 << i);
            sclLow();
        }
        if (ack) sdaLow(); else sdaHigh();
        sclHigh();
        sclLow();
        sdaHigh();
        return data;
    }
};

#endif // SOFT_I2C_H
