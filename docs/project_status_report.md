# Advanced SLI — Project Status Report

**Updated:** 21 September 2026 (first written 20 September 2026)
**See also:** [software_design_plan.md](./software_design_plan.md) (original plan) ·
[software/software.md](../software/software.md) (what is built) ·
[project_checklist.md](./project_checklist.md) (live to-do list)

---

## Where the project stands

The software is **code-complete for Phases 1–3**. The first two phases were built and run on the hardware.
Everything added since 20 September (marked ◐ below) has only been tested on a PC, against a simulated
ESP32 and with unit tests. **It has not yet been flashed or run on the crane**, so the next milestone is
flashing the firmware and checking it on the hardware.

| Phase | Description | Status |
|---|---|---|
| **Phase 1** | It Talks — end-to-end data pipeline | ✅ Complete (run on hardware) |
| **Phase 2** | It Looks Good — full UI + controls | ✅ Complete (run on hardware) |
| **Phase 3** | It's Smart — load calculation & safety | ◐ Code complete, tested on a PC only |
| **Phase 4** | It Prevents Disaster — PID anti-sway | 🔴 Not started |

Legend: ✅ works on the hardware · ◐ written and tested on a PC, not yet run on the crane · 🔴 not done.

---

## What is built

### Layer 1 — ESP32 firmware (FreeRTOS)
- ✅ Tasks: `SensorTask` (100 Hz, core 1), `TelemetryTask` (50 Hz, core 0), `CommandTask` (event-driven, core 0)
- ◐ `SafetyTask` (50 Hz, core 1, highest priority): corrected load, safe load from the load chart, load %, alarm level (80 % warning / 100 % critical, with hysteresis) and status flags
- ✅ `$T` telemetry over USB serial (15 fields) and a G-code protocol: `M1–M4`, `M0`, `T0`, `CAL0–2`, `DBG`, `CFG`
- ◐ Added: `RST`, `CAL3` (load-cell angle correction), `LC UPLOAD / SAVE / GET` (load chart in flash)
- ◐ **E-stop latch:** `T0` blocks all motion until `RST`; the Mega also disables its stepper drivers
- ◐ **Dead-man motion:** an axis stops if its command is not repeated within 400 ms (Mega and ESP32 winch)
- ◐ **Load cell fault detection:** a stale or out-of-range load cell raises a critical alarm
- ◐ **Boom-angle checks:** a dead IMU, or an IMU that disagrees with the boom encoder by more than 3° for 0.5 s, raises a critical alarm. All alarms are **alarm only**; nothing blocks motion
- ✅ Sensor drivers: `AS5600Driver` (3 buses), `MPU6050Driver`, `HX711Driver`, `FSRReader`, `N20Encoder`
- ✅ Flash (NVS) storage of the telescope scale and direction and of the IMU calibration; ◐ also the load chart, angle corrections and boom-encoder reference

### Layer 2 — Python backend (FastAPI)
- ✅ REST + WebSocket on `localhost:8000` (localhost only by default), 50 Hz telemetry broadcast, CSV session logger, command validation
- ◐ Load chart upload that waits for the ESP32 to confirm (`GET /api/loadchart` reads it back), `CAL3` endpoints
- ◐ Automated tests (84)

### Layer 3 — Dashboard (React + Vite)
- ✅ Dark industrial UI; Dashboard with live telemetry, gauges, 2-D crane view and outrigger view; hold-to-move motor controls; Debug & Calibrate tab (I2C scan, tare, IMU zero, telescope calibration with ruler tool); Settings
- ◐ Data Logger (live charts, saved sessions), Load Chart editor, Xbox controller (B = E-stop), Reset E-stop, load-cell angle correction panel, a System Status list driven by firmware flags

### Arduino Mega executor
- ✅ 3 stepper axes on RAMPS 1.4 (Swing X, Telescope Y, Boom lift Z), 250–2500 steps/s
- ◐ Latched E-stop that disables the drivers, and the dead-man timeout

### Custom vs standard libraries
- **Custom drivers, because no library fits:** `AS5600Driver` (three AS5600s that all use address 0x36 on three buses), `SoftI2C` (a third I2C bus by bit-banging), `HX711Driver` (non-blocking, so it does not starve FreeRTOS).
- **Standard:** `Adafruit_MPU6050`, `Preferences` (flash), FreeRTOS (bundled with the ESP32 core), FastAPI, uvicorn, pyserial, React 19, Vite, Lucide React, the browser Gamepad API.

---

## Current issues (hardware — unchanged since the 20 September report)

### 🔴 Telescope extension — encoder jumpiness
- The AS5600 on soft I2C (GPIO32/33) gave spikes at the 0°/360° wrap: 30 mm of travel read as 110 mm.
- Software mitigations applied: 45° glitch rejection, shortest-path accumulation, the AS5600's filtered angle register, a slower soft-I2C bit delay. It is better but still jumpy under vibration.
- Likely causes: long soft-I2C wires (capacitance), inconsistent magnet gap. Needs a mechanical fix: tighter motor-to-stage coupling, smaller magnet gap, shorter wiring.

### 🔴 N20 winch — one direction not working
- The motor only runs in reverse; forward (DRV8833 AIN1/AIN2 on GPIO13/14) produces no motion. A half-bridge of the DRV8833 is probably dead: replace the module and re-check against `n20_test`.
- `N20_TICKS_PER_REV = 840` and `ROPE_DRUM_DIAMETER_MM = 20` in `config.h` are guesses. Measure them with the firmware's own counting (rising edges of channel A) once the motor works.

### 🟡 Load cell
- Only roughly calibrated (`HX711_DEFAULT_SCALE = 122000`). A proper two-point calibration on the final assembly is still needed, and then the per-angle correction (Debug tab) with known weights.

### 🟡 IMU not calibrated on the final hardware
- Static calibration on the level, assembled crane is still to do (also sets the boom-encoder reference).

### 🟡 Outriggers, hook and rope
- Outriggers are printed and assembled but the four FSRs do not read equally under a centred load.
- The hook is designed (CAD) but not built; there is no rope or drum assembly yet.

### Not fitted
- No limit switches on any axis, so there is no end-of-travel protection in firmware yet.

---

## Remaining work

The full list, with priorities, is in [project_checklist.md](./project_checklist.md). In short:

1. **Flash the firmware and test on the crane** (E-stop, dead-man stop, fault alarms, load chart, calibration).
2. Fix the hardware issues above: DRV8833, telescope coupling and wiring, hook and rope.
3. Calibrate: load cell, IMU, telescope; record the load-cell angle correction; upload the real load chart.
4. Phase 4: PID anti-sway on the swing axis (lower priority).
5. Later: limit switches, tipping prediction from the FSRs, operator scoring, real-crane sensor integration.
