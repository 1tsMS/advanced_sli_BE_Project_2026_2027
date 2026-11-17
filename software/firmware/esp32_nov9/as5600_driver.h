// ============================================================
//  AS5600 12-bit Magnetic Rotary Encoder Driver (software I2C)
//
//  Every AS5600 has the fixed address 0x36, so each sits on its own
//  SoftI2C bus. Only the task that owns that bus may call the methods that
//  touch it (begin, readRaw, readMagnetStatus); other tasks only read the
//  cached flags and counters.
// ============================================================
#ifndef AS5600_DRIVER_H
#define AS5600_DRIVER_H

#include "config.h"
#include "soft_i2c.h"

/** Magnet health as reported by the AS5600 itself. */
struct AS5600MagnetStatus {
    bool     valid;      // the registers were read successfully
    bool     detected;   // MD: magnet detected
    bool     tooWeak;    // ML: field too weak (magnet too far / too small)
    bool     tooStrong;  // MH: field too strong (magnet too close)
    uint8_t  agc;        // automatic gain: mid-range is ideal (0..128 at 3.3 V, 0..255 at 5 V)
    uint16_t magnitude;  // CORDIC magnitude (field strength, 12-bit)
};

class AS5600Driver {
public:
    explicit AS5600Driver(SoftI2C* bus) : _bus(bus) {}

    /** Probe the sensor (non-blocking: three quick address probes). */
    bool begin();

    /** Cached: true while reads are succeeding. Never touches the bus. */
    bool isConnected() const { return _connected; }

    /**
     * Read the raw 12-bit angle (0-4095) as the wrap-aware median of three
     * back-to-back reads, so one corrupted transfer can't produce a spike.
     * A sensor marked disconnected is re-probed at most every 100 ms.
     * @return false if no trustworthy value could be read (out is untouched)
     */
    bool readRaw(uint16_t* out);

    /** Read STATUS, AGC and MAGNITUDE (magnet placement diagnostics). */
    bool readMagnetStatus(AS5600MagnetStatus* out);

    /** Single-transfer reads attempted / failed since boot (wiring quality). */
    uint32_t readCount() const { return _reads; }
    uint32_t failCount() const { return _fails; }

private:
    SoftI2C*  _bus;
    bool      _connected = false;
    uint8_t   _missCount = 0;
    uint32_t  _lastProbeMs = 0;
    volatile uint32_t _reads = 0;
    volatile uint32_t _fails = 0;
    uint16_t  _lastGoodRaw = 0;     // last accepted raw angle (single-read fallback)
    bool      _hasLastGood = false;

    // Register addresses (datasheet section 8)
    static const uint8_t REG_STATUS    = 0x0B;  // MD (bit 5), ML (bit 4), MH (bit 3)
    static const uint8_t REG_RAW_ANGLE = 0x0C;  // 12-bit unscaled angle
    static const uint8_t REG_AGC       = 0x1A;
    static const uint8_t REG_MAGNITUDE = 0x1B;  // 12-bit

    // Two good reads must agree within this many counts (~2.1 deg)
    static const int32_t AGREE_COUNTS = 24;
    // Single-read fallback: accepted only within this of the last good value (~8.4 deg)
    static const int32_t SINGLE_READ_GATE = 96;
    // Consecutive failed samples before the sensor is marked disconnected (~0.5 s)
    static const uint8_t MISS_LIMIT = 50;

    bool readAngleOnce(uint16_t* out);
    bool readBytes(uint8_t reg, uint8_t* buf, uint8_t len);
};

#endif // AS5600_DRIVER_H
