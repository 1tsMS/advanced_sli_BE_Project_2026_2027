// ============================================================
//  ZV input shaper on a speed reference (pure logic, host-tested)
//
//  output(t) = 0.5 * ref(t) + 0.5 * ref(t - T/2)
//  Every change of the reference is applied in two equal halves, T/2 apart.
//  For an undamped pendulum of period T the sway excited by the first half
//  is cancelled by the second, so the load ends the move without swinging.
// ============================================================
#ifndef ZV_SHAPER_H
#define ZV_SHAPER_H

#include <stdint.h>

class ZvShaper {
public:
    static const uint8_t QUEUE = 8;

    void reset() { _ref = _late = 0.0f; _head = _count = 0; }

    void setHalfPeriod(uint32_t ms) { _halfMs = ms; }
    uint32_t halfPeriod() const { return _halfMs; }

    /** Current (unshaped) reference: 0 = operator not commanding anything. */
    float ref() const { return _ref; }

    /** New reference at nowMs: its first half applies now, the second T/2 later. */
    void setRef(float ref, uint32_t nowMs) {
        if (ref == _ref) return;          // held buttons repeat the same command
        _ref = ref;
        if (_count == QUEUE) {            // full: apply the oldest pending half now
            _late = _q[_head].value;
            _head = (uint8_t)((_head + 1) % QUEUE);
            _count--;
        }
        Event& e = _q[(_head + _count) % QUEUE];
        e.atMs  = nowMs + _halfMs;
        e.value = ref;
        _count++;
    }

    /** Shaped speed at nowMs (signed). */
    float update(uint32_t nowMs) {
        while (_count && (int32_t)(nowMs - _q[_head].atMs) >= 0) {
            _late = _q[_head].value;
            _head = (uint8_t)((_head + 1) % QUEUE);
            _count--;
        }
        return 0.5f * (_ref + _late);
    }

private:
    struct Event { uint32_t atMs; float value; };
    Event    _q[QUEUE];
    uint8_t  _head = 0, _count = 0;
    uint32_t _halfMs = 0;
    float    _ref = 0.0f;   // reference now
    float    _late = 0.0f;  // reference as it was T/2 ago
};

#endif // ZV_SHAPER_H
