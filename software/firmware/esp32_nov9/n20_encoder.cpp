// ============================================================
//  N20 Encoder (PCNT) + DRV8833 Motor — Implementation
// ============================================================
#include "n20_encoder.h"
#include "driver/pcnt.h"

static const pcnt_unit_t N20_PCNT_UNIT = PCNT_UNIT_0;

// The legacy PCNT driver resets the counter to 0 when it reaches a limit,
// so counts are folded modulo this range in poll().
static const int16_t PCNT_LIMIT = 10000;

// Glitch filter in APB clock cycles (80 MHz): 1023 = 12.8 us, the maximum.
// Real edges are far slower: even at ~15000 motor RPM with 7 lines the
// edges are > 100 us apart, so only short noise spikes are rejected.
static const uint16_t PCNT_FILTER_CYCLES = 1023;

void N20Encoder::begin() {
    // Hall outputs on most N20 encoders are open-drain: keep the pull-ups
    pinMode(N20_ENC_A, INPUT_PULLUP);
    pinMode(N20_ENC_B, INPUT_PULLUP);

    // 4x quadrature decoding with two channels on one unit
    // (same configuration as the ESP-IDF rotary encoder example).
    pcnt_config_t c = {};
    c.unit           = N20_PCNT_UNIT;
    c.counter_h_lim  = PCNT_LIMIT;
    c.counter_l_lim  = -PCNT_LIMIT;

    c.channel        = PCNT_CHANNEL_0;
    c.pulse_gpio_num = N20_ENC_A;
    c.ctrl_gpio_num  = N20_ENC_B;
    c.pos_mode       = PCNT_COUNT_DEC;
    c.neg_mode       = PCNT_COUNT_INC;
    c.lctrl_mode     = PCNT_MODE_KEEP;
    c.hctrl_mode     = PCNT_MODE_REVERSE;
    pcnt_unit_config(&c);

    c.channel        = PCNT_CHANNEL_1;
    c.pulse_gpio_num = N20_ENC_B;
    c.ctrl_gpio_num  = N20_ENC_A;
    c.pos_mode       = PCNT_COUNT_INC;
    c.neg_mode       = PCNT_COUNT_DEC;
    pcnt_unit_config(&c);

    pcnt_set_filter_value(N20_PCNT_UNIT, PCNT_FILTER_CYCLES);
    pcnt_filter_enable(N20_PCNT_UNIT);
    pcnt_counter_pause(N20_PCNT_UNIT);
    pcnt_counter_clear(N20_PCNT_UNIT);
    pcnt_counter_resume(N20_PCNT_UNIT);

    _lastHw = 0;
    _count  = 0;
    _ready  = true;

    // DRV8833: set the inputs to brake first, then wake the driver (EEP held low
    // by its pull-down until now, so nothing moved during boot / flashing)
    pinMode(DRV_AIN1, OUTPUT);
    pinMode(DRV_AIN2, OUTPUT);
    stopMotor();
    pinMode(DRV_SLEEP, OUTPUT);
    digitalWrite(DRV_SLEEP, HIGH);
}

void N20Encoder::poll() {
    if (!_ready) return;
    int16_t hw = 0;
    if (pcnt_get_counter_value(N20_PCNT_UNIT, &hw) != ESP_OK) return;

    int32_t delta = (int32_t)hw - (int32_t)_lastHw;
    // The counter jumps back to 0 when it reaches +/-PCNT_LIMIT: fold the delta
    if (delta >  PCNT_LIMIT / 2) delta -= PCNT_LIMIT;
    if (delta < -PCNT_LIMIT / 2) delta += PCNT_LIMIT;

    _lastHw = hw;
    _count  = _count + delta;
}

void N20Encoder::setMotor(uint8_t speed, uint8_t dir) {
    if (speed == 0) { stopMotor(); return; }
    if (WINCH_REVERSED) dir ^= 1;   // physical wiring is the other way round (config.h)
    if (dir == 0) {
        // Wind up: AIN1 = PWM, AIN2 = LOW
        analogWrite(DRV_AIN2, 0);
        analogWrite(DRV_AIN1, speed);
    } else {
        // Pay out: AIN1 = LOW, AIN2 = PWM
        analogWrite(DRV_AIN1, 0);
        analogWrite(DRV_AIN2, speed);
    }
}

void N20Encoder::stopMotor() {
    analogWrite(DRV_AIN1, 255);
    analogWrite(DRV_AIN2, 255);
}
