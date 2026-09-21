# Hardware

The hardware is a **scale-model truck-mounted crane** with four motorised axes, used to develop and
validate the Safe Load Indicator (SLI) sensing and safety logic before any real-crane work.

Wiring and pin assignments are in [connections.md](connections.md). CAD files are in
[cad_model/V1](cad_model/V1).

## Axes

| Axis | Command | Actuator | Position feedback |
|---|---|---|---|
| Swing (slew) | `M1` | NEMA17 via RAMPS X | AS5600 on I2C bus 0 |
| Boom lift | `M2` | NEMA17 via RAMPS Z | AS5600 on the boom pivot shaft (I2C bus 1, 1:1) **and** an MPU6050 IMU on the boom |
| Telescope extension | `M3` | NEMA17 via RAMPS Y | AS5600 on I2C bus 2 (angle converted to mm) |
| Winch (cable reel) | `M4` | N20 gear motor, 150 RPM, via DRV8833 | Encoder built into the N20 (rope length) |

## Sensors

| Sensor | Purpose |
|---|---|
| Load cell (5 kg) + HX711 | Load on the hook. It sits on top of the boom with the cable running over it, so its reading depends on the boom angle. The firmware learns a per-angle correction from known weights (see `software/software.md`). |
| MPU6050 (6-axis IMU) on the boom | Boom elevation angle (used for the load chart) and sideways lean of the boom |
| 3 × AS5600 magnetic encoders | Swing angle, boom angle (cross-check for the IMU), telescope extension |
| 4 × FSR (RP-C18.3-ST) under the outriggers | Load distribution between the four outrigger feet |
| N20 built-in encoder | Rope length paid out |

## Controllers and power

| Part | Role |
|---|---|
| ESP32 (30-pin) | Runs the sensors, safety task and USB link to the PC |
| Arduino Mega 2560 + RAMPS 1.4 + 3 × A4988 | Steps the three stepper axes on command from the ESP32 |
| DRV8833 | Drives the N20 winch motor from the ESP32 |
| 12 V, 10 A supply | Motors and RAMPS; the ESP32 runs from USB |

## Bill of materials

| Component | Specification | Qty | Purpose |
|---|---|---|---|
| NEMA17 stepper motor | 17HS8401 | 3 | Swing, boom lift, telescope |
| N20 DC gear motor | 150 RPM, with encoder | 1 | Winch |
| ESP32 dev board | 30-pin | 1 | Sensors + safety + PC link |
| Arduino Mega 2560 | — | 1 | Stepper executor |
| RAMPS 1.4 | — | 1 | Stepper driver shield |
| A4988 stepper driver | — | 4 (3 used) | Stepper drivers |
| DRV8833 | — | 1 | N20 driver |
| AS5600 magnetic encoder | — | 3 | Swing, boom, telescope angles |
| MPU6050 IMU | 6-axis | 1 | Boom angle and lean |
| HX711 + load cell | 5 kg | 1 | Load |
| FSR | RP-C18.3-ST | 4 | Outrigger load |
| Limit switches | mechanical | — | **Not fitted yet** |
| Power supply | 12 V, 10 A | 1 | Motors |

## Mechanical design (V1, Fusion 360 → 3D printed, plus laser-cut base)

Files in `cad_model/V1`:

| Group | Files |
|---|---|
| Base and outriggers | `base plate.stl`, `base plate.dxf`, `base plate2.dxf` (DXF for laser cutting), `outrigger.stl`, `outrigger_plate.stl`, `support.stl` |
| Boom | `boom.stl`, `boom tip.stl`, `extension.stl`, `extension offset.stl` |
| Winch and hook | `drum.stl`, `drum offset.stl`, `hook.stl`, `loadcell cap.stl` |
| Drivetrain and mounts | `worm.stl`, `yoke.stl`, `housing.stl`, `housing offset.stl`, `coupling.stl`, `coupling 2.stl`, `mount.stl`, `cap.stl` |
| Encoder mounts | `encoder.stl`, `magnet mount.stl` |
| Other | `Body2.stl`, `Body3.stl`, `Body4.stl` (unnamed bodies from the CAD export), `all.3mf` (whole assembly) |

Several parts have an "offset" or "2" variant. Which one is current is not documented yet; note it here when the design is frozen.

## Status of the hardware (as of the September 2026 status report)

Assembled and working: swing, boom lift and telescope steppers; the AS5600s, MPU6050, HX711 and FSRs (reading, not yet calibrated); outriggers (printed and assembled).

Open issues:

- **Telescope encoder** is jumpy (long soft-I2C wires, magnet gap, coupling); mechanical fix needed.
- **N20 winch** only turns one way, probably a dead DRV8833 half-bridge; replace the module.
- **Hook and rope** are not built yet; the hook CAD exists, and the N20 encoder ticks per revolution and drum diameter in `config.h` are still guesses.
- **Load cell** is only roughly calibrated; the two-point calibration and per-angle correction still have to be done on the final assembly.
- **Outrigger FSRs** do not read equally under a centred load.
- **No limit switches** yet.

The live to-do list is in `docs/project_checklist.md`.
