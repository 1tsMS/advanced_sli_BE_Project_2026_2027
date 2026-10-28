# ESP32 firmware v2 (`esp32_mainv2`)

Firmware for the ESP32 on the scale-model crane. It reads every sensor, runs the
Safe Load Indicator (SLI), and drives the motors: the three steppers through the
Arduino Mega (UART), the N20 winch directly through a DRV8833. The PC dashboard
talks to it over USB serial at 115200 baud.

Built on FreeRTOS: each job runs in its own task with a fixed core, priority and
rate, so a slow sensor or a flash write can never stall the safety logic.

## Contents

1. [Toolchain and libraries](#1-toolchain-and-libraries)
2. [Hardware map](#2-hardware-map)
3. [Architecture](#3-architecture)
4. [Boot sequence](#4-boot-sequence)
5. [File by file](#5-file-by-file)
6. [How the key parts work](#6-how-the-key-parts-work)
7. [Serial protocol](#7-serial-protocol)
8. [Calibration](#8-calibration)
9. [Diagnostics](#9-diagnostics)
10. [Tests](#10-tests)
11. [Known limits](#11-known-limits)

---

## 1. Toolchain and libraries

| Item | Version / note |
|---|---|
| Board | ESP32 Dev Module (`esp32:esp32:esp32`) |
| Arduino core | esp32 2.0.11 (ESP-IDF 4.4) |
| External library | **HX711 by Rob Tillaart, 0.6.3** (not bogde's) |

That is the only third-party library. Everything else comes with the ESP32 core
or the C standard library:

| Header | From | Used for |
|---|---|---|
| `Arduino.h` | ESP32 core | GPIO, `millis()`, `Serial`, `analogRead/Write` |
| `Preferences.h` | ESP32 core | NVS flash storage (calibration, positions, load chart) |
| FreeRTOS (via `Arduino.h`) | ESP-IDF | tasks, queues, mutex, spinlocks, notifications |
| `driver/gpio.h` | ESP-IDF | open-drain GPIO for the bit-banged I2C |
| `driver/pcnt.h` | ESP-IDF | hardware pulse counter for the N20 encoder |
| `esp_task_wdt.h` | ESP-IDF | task watchdog |
| `esp_system.h` | ESP-IDF | reset reason |
| `math.h`, `string.h`, `stdio.h`, `stdlib.h`, `ctype.h`, `stdarg.h` | C library | maths, parsing, formatting |

**Not used, on purpose:** `Wire` (hardware I2C) and `Adafruit_MPU6050`. In this
core a hardware I2C transfer that hangs on noise blocks for at least 1 s, which
froze the whole sensor loop on the crane. All three I2C buses are bit-banged
instead (see [6.1](#61-software-i2c)).

**Build** (from `software/firmware`, library folders as on the lab PC):

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries <folder with HX711> esp32_mainv2
```

---

## 2. Hardware map

All pins are defined in `config.h`.

| Function | GPIO | Notes |
|---|---|---|
| I2C bus 0 SDA / SCL | 21 / 22 | swing AS5600 + MPU6050, 5 us half-bit |
| I2C bus 1 SDA / SCL | 25 / 26 | boom AS5600, 8 us half-bit |
| I2C bus 2 SDA / SCL | 32 / 33 | telescope AS5600, 4.7k pull-ups |
| HX711 DT / SCK | 17 / 16 | load cell on the boom tip |
| FSR 1-4 | 34, 35, 36, 39 | outrigger force sensors, ADC1 (input-only pins) |
| DRV8833 AIN1 / AIN2 | 13 / 14 | winch PWM; EEP (nSLEEP) is soldered to 3.3 V |
| N20 encoder A / B | 18 / 19 | PCNT unit 0, 4x quadrature |
| UART2 to Mega RX / TX | 23 / 27 | 9600 baud, Mega pins 18/19 |
| USB serial | | 115200 baud to the PC |

GPIO12 is deliberately unused: it is a boot strapping pin and must be low at reset.

All AS5600 magnets sit on the back of the stepper motors, not on the output
shafts, so every encoder counts motor turns (multi-turn) and is scaled to the
axis: swing 20:1 cycloidal (18 deg per encoder turn), telescope leadscrew
(mm per turn), boom leadscrew (non-linear, so the boom angle comes from the IMU).

---

## 3. Architecture

### Tasks

| Task | Core | Prio | Rate | Job |
|---|---|---|---|---|
| SensorTask | 1 | 5 | 100 Hz | telescope encoder, HX711, FSRs, N20 count, **all encoder maths**, calibration, writes `SensorData` |
| SafetyTask | 1 | 4 | 50 Hz | the SLI: load % vs load chart, blocked-motion mask, E-stop latch, bypass/off, `$S` status |
| CommandTask | 0 | 5 | on USB input (+ every 20 ms) | parses commands, enforces the SLI, talks to the Mega, drives the winch, dead-man |
| SerialTxTask | 0 | 4 | 50 Hz | **only** writer of USB serial: `$T` telemetry + queued lines |
| HwBusTask | 0 | 3 | 100 Hz | swing + boom AS5600 and MPU6050 (bit-banged, busy-waits), IMU filter |
| StorageTask | 0 | 1 | on demand | **only** writer of flash (NVS) |

Core 1 holds the measurement loop and the safety logic. Core 0 holds
communication and anything that may wait. Everything is listed with its reasons
in `rtos_config.h`.

### Rules every task follows

- **One owner per resource.** Each I2C bus, the HX711, USB TX, the Mega UART, NVS
  and the encoder maths belong to exactly one task. Others hand work over through
  a queue, a task notification or a small staged copy.
- **No blocking in real-time loops.** Periodic tasks use `vTaskDelayUntil`; event
  tasks block on a notification or a queue. No `delay()` after setup.
- **Shared data:** `SensorData` is protected by `sensorMutex` (short holds,
  timeouts); small hand-over structs use spinlocks.
- **Watchdog:** every real-time task feeds the task watchdog. If one stops for 3 s
  the ESP32 resets, and the Mega's own dead-man stops the steppers within 400 ms.

### Data flow

```
             HwBusTask (core 0)                      SensorTask (core 1)
  bus0/bus1 ─► swing+boom raw, IMU angles ──_hw──►  encoder maths, telescope (bus2),
                                         (spinlock)  HX711, FSR, rope ──► SensorData
                                                                         (sensorMutex)
                                                                              │
              ┌───────────────────────────────────────────────────────────────┤
              ▼                                                               ▼
     SafetyTask (core 1)                                            SerialTxTask (core 0)
  sli_evaluate() vs load chart ─► blocked mask, state                $T every 20 ms ──► USB
  writes loadPercent / limit /     (spinlock) ──notify──┐            + queued lines ($ACK,$S,$D,$LC)
  alarmLevel into SensorData                            ▼
                                               CommandTask (core 0)
  USB command ──────────────────────────────► parse ─► allowed? ─► Mega (UART2) / DRV8833
                                                          │
                                             stop axes that became blocked
```

---

## 4. Boot sequence

`esp32_mainv2.ino` → `setup()` runs once, before any task:

1. USB serial up, reset reason printed (POWER_ON, BROWNOUT, TASK_WATCHDOG, ...).
2. Task watchdog (3 s), `sensorMutex`, serial output queue.
3. The three I2C buses: pins configured, stuck buses freed (9 clock pulses + STOP).
4. Sensors probed: three AS5600, MPU6050 (then the gyro is calibrated: **keep the
   boom still at power-up**), HX711, FSR ADC, N20 PCNT + DRV8833, Mega UART.
5. Calibration restored from flash (`sensorTask_loadCalibration`) and the load
   chart restored (`safety_init`). No chart in flash = the SLI starts in FAULT.
6. Tasks started, writers first (Storage, SerialTx), then Command, HwBus, Sensor,
   Safety. USB input is wired to wake CommandTask.
7. `loop()` deletes itself: everything runs in tasks.

---

## 5. File by file

### Entry point and configuration

| File | What it does |
|---|---|
| `esp32_mainv2.ino` | `setup()`: hardware init, calibration restore, task start. Owns the global objects (buses, drivers, `sensorData`, task handles). |
| `config.h` | Every pin, I2C address, scale constant and timing limit; the shared structs `SensorData`, `MotorCommand`, `CalCommand`. |
| `rtos_config.h` | Task table: cores, priorities, stack sizes, periods, watchdog timeout, task handles. |

### Buses and sensor drivers

| File | What it does |
|---|---|
| `soft_i2c.h` | Bit-banged I2C master on open-drain GPIO. Cannot hang; detects a clock line that won't rise. API mirrors `Wire`. |
| `as5600_driver.h/.cpp` | AS5600 magnetic encoder: reads the 12-bit raw angle three times, takes the median, rejects bus garbage; reads magnet health (too weak / too strong). |
| `encoder_axis.h/.cpp` | Turns raw AS5600 angles into an axis value: multi-turn counting, glitch gate, resync, reference/zero, invert, units per turn, save/restore. Pure logic. |
| `mpu6050_driver.h/.cpp` | MPU6050 by direct register access: configure, read, reject corrupt samples, gyro calibration, complementary filter for roll/pitch. |
| `hx711_driver.h/.cpp` | Wraps the HX711 library non-blocking: one conversion per call when ready, tare and averaging as background state machines, staleness check. |
| `fsr_reader.h` | Four FSRs on ADC1, raw 0-4095. |
| `n20_encoder.h/.cpp` | N20 encoder on the ESP32 PCNT hardware (4x quadrature, glitch filter) and the DRV8833 winch outputs (PWM, brake). |

### Measurement

| File | What it does |
|---|---|
| `sensor_task.h/.cpp` | SensorTask and HwBusTask. Builds `SensorData` at 100 Hz, runs all calibration commands, saves positions when an axis is still, collects diagnostics. |

### Safety (SLI)

| File | What it does |
|---|---|
| `load_chart.h/.cpp` | Load chart data, upload parser (`LC ...` lines), validation, bilinear capacity lookup, swing zones. Pure logic. |
| `safety_logic.h/.cpp` | `sli_evaluate()`: load %, state (OK/WARN/CUT/FAULT) with hysteresis, and which motions are blocked. Pure logic. |
| `safety_task.h/.cpp` | SafetyTask: runs the logic at 50 Hz, owns the active chart, E-stop latch, bypass timer, OFF mode, `$S` status, and the API CommandTask uses. |

### Commands and communication

| File | What it does |
|---|---|
| `gcode_parser.h/.cpp` | Text command parser (`M2 S500 D1`, `T0`, `CAL7 W0.5`, `LC ...`, `SLI ...`). Clamps values; no side effects. |
| `command_task.h/.cpp` | CommandTask: dispatch, SLI enforcement, winch dead-man, DBG report, motion tracking so a motion that becomes blocked is stopped. |
| `mega_bridge.h/.cpp` | Formats motor lines for the Mega over UART2 (`M1-3`, `M0 Ax`, `T0`). The winch (axis 4) never goes to the Mega. |
| `serial_out.h/.cpp` | Line queue in front of USB TX: any task can `serialLine()` without blocking; full queue = line dropped and counted. |
| `telemetry_task.h/.cpp` | SerialTxTask: `$T` on a fixed schedule, queued lines in between. |
| `telemetry_fmt.h/.cpp` | Formats `$T` and the multi-line `$D` debug report. |

### Storage

| File | What it does |
|---|---|
| `persist.h/.cpp` | Real-time tasks stage new settings in RAM; StorageTask writes only the changed groups to NVS. Boot-time reads happen directly in `setup()`. |

---

## 6. How the key parts work

### 6.1 Software I2C

`soft_i2c.h` drives SDA/SCL as true open-drain pins (`GPIO_MODE_INPUT_OUTPUT_OD`):
writing 1 releases the line and the pull-up makes it high, writing 0 pulls it
low. Timing is a fixed half-bit delay (5 us is about 80-100 kHz).

- A transfer cannot hang: a corrupted one fails in well under a millisecond and
  the caller retries next cycle.
- After every clock release the pin is read back. Nothing on these buses
  clock-stretches, so a low SCL means a wiring fault (broken or shorted clock
  wire). The transfer is then thrown away instead of returning bits sampled off
  a dead clock, and the fault is counted.
- `recoverBus()` frees a slave that holds SDA low after a reset (up to 9 clocks + STOP).

### 6.2 Encoders (AS5600 + EncoderAxis)

Each read in `as5600_driver.cpp` takes three raw samples and accepts the median
if two agree within 24 counts; a single sample is accepted only near the last
good value. Any read with the top nibble set is a bus error (the AS5600 is 12-bit).

`encoder_axis.cpp` then:

- **Counts turns:** the shortest-way difference between samples (wraps 4095 → 0)
  is added to a running count, so a motor-side magnet gives a multi-turn position.
- **Glitch gate:** a jump larger than the shaft could physically turn since the
  last *accepted* sample (51 counts/ms, max 1800) is rejected. Five consistent
  rejected samples in a row are accepted as the new truth (resync).
- **Scales:** value = reference + sign × counts × units-per-turn / 4096.
- **Saves and restores:** when an axis has been still for 1 s and moved more than
  0.5 units, its reference, counts and raw angle are staged to flash. At boot the
  saved zero is rebuilt from those, plus the small movement made while powered off.

Swing and boom raw angles are read by HwBusTask; the maths for all three axes
runs only in SensorTask, so there is one owner and no race.

### 6.3 IMU (MPU6050)

- Configured to ±8 g, ±500 deg/s, on-chip low-pass filter 5 Hz.
- **Rejected samples:** total acceleration outside 0.5-2 g, any gyro axis above
  300 deg/s, or a gyro step above 100 deg/s between 10 ms samples (impossible
  behind the 5 Hz filter). A new gyro level that holds for 5 samples is accepted.
- **Complementary filter** at 100 Hz:
  `angle = pred + (1 - a) × wrap(accelAngle - pred)` with `pred = angle + gyro × dt`
  and `a = 0.99` (time constant about 1 s). The gyro tracks motion; the
  accelerometer slowly removes drift. The `wrap()` keeps it correct near ±180°.
- **Zero (CAL1):** averages the gyro for about 1 s (boom still) and stores the
  current angle as the boom zero, plus which axis is the boom and which the tilt.
- On a dropout the driver re-initialises every 0.5 s; meanwhile SensorTask holds
  the last good angle and reports `sensorsOK = false`.

### 6.4 Load cell (HX711)

The HX711 converts at 10 SPS. `getWeight()` reads only when a conversion is
ready, otherwise returns the last value, so the 100 Hz loop never waits.
Weight = (raw - offset) / scale, with a ±20 g dead band around zero.

- **Tare (CAL0):** averages 10 conversions in the background, saves the offset.
- **Scale (CAL7 W<kg>):** averages 20 conversions with a known weight hanging,
  then refits counts-per-kg through zero over every weight recorded this session
  (least squares) and saves it.
- **Stale = fault:** a broken HX711 wire stops conversions but leaves the last
  weight in place. No conversion for 500 ms marks the sensors as not OK.

### 6.5 Winch (N20 + DRV8833)

- `setMotor(pwm, dir)`: dir 0 winds up (AIN1 = PWM, AIN2 = 0), dir 1 pays out.
  The command speed 0-2500 is mapped to PWM 0-255.
- `stopMotor()`: both inputs high = DRV8833 brake, so the drum holds.
- Rope length comes from the PCNT count: rope mm = ref + sign × (count - zero) × mm-per-count.
- Dead-man in CommandTask: no M4 for 400 ms = stop.

### 6.6 The SLI

**Load chart** (`load_chart.cpp`): rows are boom angles, columns are telescope
extensions, each cell is the rated payload in kg (up to 8 × 6). Capacity between
cells is bilinear. Up to 4 swing zones multiply the whole chart (for example
×0.5 over the rear). Outside the chart, beyond 1° / 2 mm of tolerance, capacity
is 0: operation there is not allowed, so the chart edges are soft travel limits.

**Decision** (`safety_logic.cpp`), every 20 ms:

1. No valid chart, a sensor down, or an impossible load reading → **FAULT**.
   Only the moves that always reduce the load moment are allowed: boom up,
   retract, winch pay out.
2. Otherwise load % = load / capacity × 100, and the state is OK / WARN (≥ warn %)
   / CUT (≥ cut %), with 2 % hysteresis so it doesn't flicker at a threshold.
3. For each axis and direction the load % is computed **one step ahead**
   (1° boom, 2 mm extension, 3° swing). That direction is blocked if the step
   lands at or above the cut % and does not lower the load %. So:
   - at 100 %, boom down and extend are cut, boom up and retract still work;
   - a move is cut one step **before** it reaches 100 % (predictive cut);
   - outside the chart, the % grows with the distance outside, so moving back in
     always counts as an improvement and the crane can't get stuck.
4. Winch up (hoist) can only add load, so it is cut while the state is CUT.

**Enforcement** (`command_task.cpp`): before forwarding a motion it calls
`safety_motionAllowed()`. A refused command gets one `$ACK,SLI,REFUSED,...` per
press. When SafetyTask sees a newly blocked direction it wakes CommandTask, which
stops any axis already moving that way and sends `$ACK,SLI,CUT,...`. Worst case
from limit to stop command: about 20 ms + one UART line to the Mega (~10 ms).

**Modes:**

| Mode | Motion cut | Ends |
|---|---|---|
| ARMED | yes | default at every boot |
| BYPASS | no | after 1-600 s, `SLI ON`, or reboot |
| OFF | no | `SLI ON` or reboot (not saved on purpose) |

The E-stop latch (`T0`, cleared by `RST`) and the 400 ms dead-man work in every
mode. In BYPASS and OFF the state and blocked mask are still computed and
reported, so the dashboard can show what the SLI would do.

### 6.7 Layers of stopping

| Layer | Where | Triggers |
|---|---|---|
| Dead-man | Mega + ESP32 winch | no repeated motion command for 400 ms |
| SLI cut | ESP32 | overload, chart edge, sensor fault |
| E-stop latch | ESP32 + Mega | `T0`; nothing moves until `RST` |
| Watchdog | ESP32 | a real-time task stuck 3 s → reset (then the Mega dead-man) |

All of these are software. There is no hardware E-stop yet (see [Known limits](#11-known-limits)).

### 6.8 Flash writes

Writing NVS stalls code running from flash on **both** cores for milliseconds.
So real-time tasks never call `Preferences` after boot: they copy the new value
into a staging struct under a spinlock and notify StorageTask. StorageTask waits
200 ms to merge bursts, then writes only the dirty groups, and retries failed
ones every second. Namespaces: `sli_pos2` (positions), `sli_imu` (IMU + telescope),
`sli_enc` (swing/boom), `sli_rope`, `sli_hx` (tare + scale), `sli_lc` (load chart).

### 6.9 Serial output

Only SerialTxTask writes to USB. Every other task formats a complete line with
`serialLine()` and drops it into a 48-line queue without waiting. Lines never
interleave, a large DBG report can't stall the sensor loop, and if the queue is
ever full the line is dropped and counted (shown in DBG) instead of blocking.

---

## 7. Serial protocol

### Commands (PC → ESP32, one per line)

| Command | Meaning |
|---|---|
| `M1 S<speed> D<0\|1>` | swing (to the Mega) |
| `M2 S<speed> D<0\|1>` | boom lift, D1 = up |
| `M3 S<speed> D<0\|1>` | telescope, D1 = extend |
| `M4 S<speed> D<0\|1>` | winch, D0 = wind up, D1 = pay out |
| `M0 A<1-4>` | stop one axis |
| `T0` | E-stop, latched |
| `RST` | clear the E-stop latch |
| `SLI ON` / `SLI OFF` / `SLI BYPASS <s>` | SLI mode |
| `LC BEGIN/A/E/R/Z/P/COMMIT/GET` | load chart upload and readback (see `load_chart.h`) |
| `CAL0` ... `CAL8` | calibration (section 8) |
| `DBG` | debug report |
| `CFG RATE <hz>` | telemetry rate |

Speed is steps/s (1-2500). Motion commands must repeat every 100 ms while a
button is held; that is the dead-man.

### Lines out (ESP32 → PC)

| Prefix | Content |
|---|---|
| `$T` | telemetry, 50 Hz: boomAngle, extensionMM, measuredLoad, actualLoad, swingAngle, ropeLengthMM, fsr1-4, imuRoll (IMU boom angle), imuPitch (tilt), safeLoadLimit, loadPercent, alarmLevel |
| `$S` | SLI status, 5 Hz + on change: state, pct, capKg, zone, bypassLeftS, blockedHex, chart, mode |
| `$ACK,...` | command results (`$ACK,CAL7,OK,...`, `$ACK,SLI,REFUSED,A2,D0,CUT`, `$ACK,LC,COMMITTED,<name>`, ...) |
| `$D,...` | debug report lines, ending with `$D,END` |
| `$LC,...` | load chart readback, ending with `$LC,END` (or `$LC,NONE`) |

`alarmLevel`: 0 OK, 1 WARN, 2 CUT or FAULT, 3 E-stop.
`blockedHex` bit `(axis-1)*2 + dir` set = that motion is blocked.

---

## 8. Calibration

All results are saved to flash and survive reboots.

| Command | Does |
|---|---|
| `CAL0` | tare the load cell (nothing hanging) |
| `CAL7 W<kg>` / `CAL7 CLEAR` / `CAL7 S<counts/kg>` | record a known weight and refit the scale / clear points / set the scale |
| `CAL1 B<0\|1> I<0\|1> T<0\|1> Q<0\|1>` | zero the IMU; B = boom axis, I = invert, T = tilt axis, Q = invert |
| `CAL2 S<mm/rev> I<0\|1>` | telescope scale and direction |
| `CAL4 A<1-4> V<value>` | "this axis is at value now" (deg, or mm; A4 = rope) |
| `CAL5 A<1-4> I<0\|1>` | flip an axis direction |
| `CAL6 S<mm/count>` | rope scale |
| `CAL8 A<1-3> S<units/rev> [I<0\|1>]` | units per encoder turn (swing / boom / telescope) |

Recommended order: tare → known weights → IMU zero → telescope → swing → rope,
then upload a load chart. The Debug & Calibrate tab in the dashboard sends these.

---

## 9. Diagnostics

`DBG` returns a `$D` report (shown in the Debug tab): each I2C device, magnet
health and read/fail/reject counts per encoder, IMU re-inits and rejected samples,
HX711 raw counts and scale, FSR values, loop timing (average, worst, per stage),
free stack per task, dropped serial lines, flash writes/failures, free heap and
the last reset reason.

Reading it:

- `reset: BROWNOUT` → supply dips (motors), not a code bug.
- encoder `fails` or IMU `rejected` climbing → bus noise or a bad wire.
- `SENSOR_LOOP period_max` well above 10 ms → something is blocking the loop.

---

## 10. Tests

The pure-logic files (`encoder_axis`, `load_chart`, `safety_logic`) have no
Arduino dependency and are tested on the PC with g++ from `software/firmware`:

```bash
g++ -std=c++17 -Wall -Wextra -I esp32_mainv2 host_tests/test_safety.cpp esp32_mainv2/load_chart.cpp esp32_mainv2/safety_logic.cpp -o host_tests/test_safety && host_tests/test_safety
```

They cover the chart parser and validation, interpolation, swing zones, FAULT,
the selective cut, the predictive cut, hysteresis, chart-edge limits and the
"outside the chart" recovery case.

---

## 11. Known limits

- **Boom angle comes from the IMU**, at the boom tip, so it includes boom flex
  under load and needs about 1 s to settle after motion. The boom encoder is on
  the leadscrew motor (non-linear), so it isn't used for the angle yet.
- **Load is not angle-compensated yet:** `actualLoad` equals the raw reading.
- **Swing direction for the SLI is assumed:** `SLI_SWING_D1_SIGN` in
  `safety_logic.h` must be checked on the crane (hold Swing D1, the angle should increase).
- **No hardware E-stop:** every stop path is software. A mushroom switch cutting
  the 12 V motor supply is the cheap fix.
- **The gyro is calibrated at every boot**, so the boom must be still at power-up.
