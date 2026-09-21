// ============================================================
//  AS5600 12-bit Magnetic Rotary Encoder Driver
//  Supports all 3 I2C buses (Wire, Wire1, SoftI2C)
// ============================================================
#ifndef AS5600_DRIVER_H
#define AS5600_DRIVER_H

#include "config.h"
#include <Wire.h>

class SoftI2C;  // Forward declaration

/**
 * Driver for AS5600 contactless magnetic rotary position sensor.
 * Each AS5600 has a fixed address (0x36), so each must be on a
 * separate I2C bus. We support hardware Wire/Wire1 and software I2C.
 */
class AS5600Driver {
public:
    enum BusType { BUS_WIRE0, BUS_WIRE1, BUS_SOFT };

    AS5600Driver(BusType bus, SoftI2C* softBus = nullptr)
        : _busType(bus), _softBus(softBus),
          _connected(false), _offset(0), _lastAngle(0.0f) {}

    /** Probe the sensor. Returns true if it responds on I2C. */
    bool begin();

    /** Is the sensor currently responding? Re-probes if disconnected. */
    bool isConnected();

    /**
     * Read angle in degrees (0.0–360.0), corrected by zero offset.
     * Returns last known value if read fails.
     */
    float readAngle();

    /** Read the raw 12-bit register value (0–4095). */
    uint16_t readRawAngle();

    /** Read continuous angle in degrees, tracking multi-turn wraps. */
    float readContinuousAngle();

    /** Set current position as zero. */
    void setZero();

private:
    BusType   _busType;
    SoftI2C*  _softBus;
    bool      _connected;
    uint16_t  _offset;
    float     _lastAngle;
    float     _continuousAngle = 0.0f;
    bool      _isFirstRead = true;
    uint16_t  _lastRawAngle = 0;
    uint8_t   _missCount = 0;

    // Register address: 12-bit filtered angle
    static const uint8_t REG_ANGLE = 0x0E;

    /**
     * Read a 16-bit register via the configured I2C bus.
     * @return true on success, value written to *outValue
     */
    bool readRegister16(uint8_t reg, uint16_t* outValue);
};

#endif // AS5600_DRIVER_H
