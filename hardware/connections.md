# Advanced SLI — Protoboard Connections

> **Source of truth:** these tables match `software/firmware/esp32_main/config.h` and
> `software/firmware/mega_executor/mega_executor.ino`, and the pins used by the sketches in
> `software/test_codes/` that were confirmed working on the hardware. If you rewire something,
> change `config.h` and this file together.
>
> *Corrected 2026-09-21:* the HX711 and the ESP32↔Mega UART pins listed here previously
> (GPIO 4/27 and 16/17) did not match the firmware. GPIO 27 was also listed twice.

## Architecture

- **ESP32** = sensor acquisition + safety brain. It runs the FreeRTOS tasks and talks to the PC over USB.
- **Arduino Mega 2560 + RAMPS 1.4** = stepper motor executor (3 axes only). It has no safety logic of its own except stopping on E-stop and when commands stop arriving.
- **N20 winch (DRV8833)** is driven directly by the ESP32, not by RAMPS.

```
PC (dashboard) ──USB 115200── ESP32 ──UART2 9600── Mega + RAMPS ── 3 × NEMA17
                               │
                               ├─ I2C ×3: 3 × AS5600, MPU6050
                               ├─ HX711 → load cell
                               ├─ 4 × FSR (outriggers)
                               └─ DRV8833 → N20 winch (+ encoder)
```

---

## ESP32 (30-pin) — Sensor + Control Brain

### I2C — 3 separate buses (all AS5600 share address 0x36, so they cannot share a bus)

| Bus | Type | SDA | SCL | Device(s) |
|---|---|---|---|---|
| I2C Bus 0 | Hardware (`Wire`) | GPIO21 | GPIO22 | AS5600 — Swing, **and** MPU6050 (0x68). The MPU6050 is physically mounted on the boom but wired onto this bus. |
| I2C Bus 1 | Hardware (`Wire1`) | GPIO25 | GPIO26 | AS5600 — Boom lift (on the boom pivot shaft, 1:1) |
| I2C Bus 2 | Software / bit-banged (`SoftI2C`) | GPIO32 | GPIO33 | AS5600 — Telescope |

All buses run at about 100 kHz (bus 2 about 60 kHz). Bus 2 is bit-banged because the ESP32 has only two hardware I2C peripherals.

### Analog — Outrigger force sensors (FSR RP-C18.3-ST)

| Signal | GPIO | Notes |
|---|---|---|
| FSR 1 (front-left) | GPIO34 | ADC1, input-only pin |
| FSR 2 (front-right) | GPIO35 | ADC1, input-only pin |
| FSR 3 (rear-left) | GPIO36 | ADC1, input-only pin |
| FSR 4 (rear-right) | GPIO39 | ADC1, input-only pin |

### Load cell (top of boom, cable runs over it) — HX711

| Signal | GPIO |
|---|---|
| DT | GPIO17 |
| SCK | GPIO16 |

### Winch — N20 (encoder-integrated) via DRV8833

| Signal | GPIO | Notes |
|---|---|---|
| DRV8833 AIN1 | GPIO13 | PWM / direction |
| DRV8833 AIN2 | GPIO14 | PWM / direction |
| DRV8833 STBY | GPIO12 | Driven HIGH by the firmware. Tie to 3.3V instead if the pin is not wired. |
| N20 Encoder A | GPIO18 | Interrupt (rising edge counted) |
| N20 Encoder B | GPIO19 | Direction sense |

### Communication

| Link | ESP32 Pin | Connects To |
|---|---|---|
| UART2 RX | GPIO23 | Mega TX1 (pin 18) |
| UART2 TX | GPIO27 | Mega RX1 (pin 19) |
| UART0 (USB) | Built-in | PC (telemetry and commands to the web dashboard's backend), 115200 baud |

The ESP32↔Mega link runs at 9600 baud, 8N1. Ground must be shared between the two boards.

### Power

- 5 V / 3.3 V and GND common rail for all sensors. Check each breakout's rated voltage individually (AS5600 and MPU6050 breakouts are typically 3.3–5 V tolerant; the FSR voltage divider reference should match the ADC reference).

---

## Mega 2560 + RAMPS 1.4 — Motor Executor (3 stepper axes)

| Axis | RAMPS Slot | Driver | Motor | STEP | DIR | EN |
|---|---|---|---|---|---|---|
| Swing (`M1`) | X | A4988 | NEMA17 | 54 | 55 | 38 |
| Telescope (`M3`) | Y | A4988 | NEMA17 | 60 | 61 | 56 |
| Boom lift (`M2`) | Z | A4988 | NEMA17 | 46 | 48 | 62 |
| — | E0 | unused | (the winch moved to the ESP32) | | | |

| Signal | Mega Pin | Connects To |
|---|---|---|
| Serial1 RX1 | Pin 19 | ESP32 GPIO27 (TX2) |
| Serial1 TX1 | Pin 18 | ESP32 GPIO23 (RX2) |
| Power In | RAMPS power terminal | 12 V PSU (12 V, 10 A) |

The A4988 enable pins are active LOW. On E-stop the Mega drives them HIGH (drivers off) unless `ESTOP_DISABLES_DRIVERS` is set to 0 in `mega_executor.ino`.

---

## Not connected yet

- **Limit switches:** none are fitted, so there is no end-of-travel protection in firmware. To be added later.

## Known differences in old sketches

- `software/test_codes/conn_test` assigns the boom lift to the RAMPS **Y** slot and the telescope to **Z**. The Mega firmware (`mega_executor.ino`) and the table above use the opposite: telescope on **Y**, boom lift on **Z**. Follow the table above.
- `software/test_codes/n20_test` counts both edges of encoder channel A, while the firmware counts rising edges only, so tick counts differ by a factor of two between the two.
