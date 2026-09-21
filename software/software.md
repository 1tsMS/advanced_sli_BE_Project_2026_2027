# Software

Three layers, all in this folder:

```
software/
├── firmware/
│   ├── esp32_main/       ESP32 firmware (FreeRTOS): sensors, safety, USB protocol
│   ├── mega_executor/    Arduino Mega firmware: steps the 3 stepper axes
│   └── tests/            PC-side unit tests for the firmware logic
├── sli_desktop/
│   ├── backend/          Python FastAPI: USB serial <-> REST + WebSocket, CSV logging
│   └── dashboard/        React + Vite web dashboard
└── test_codes/           Stand-alone hardware test sketches (see its readme)
```

```
Dashboard (React, browser) ──REST + WebSocket :8000── Backend (FastAPI) ──USB 115200── ESP32 ──UART 9600── Mega + RAMPS
```

The original design is in [docs/software_design_plan.md](../docs/software_design_plan.md); its status note at the
top lists where the implementation differs. **This file describes what is actually built.**

## Safety behaviour (ESP32)

All safety decisions are made on the ESP32. The dashboard only displays them.

| Feature | Behaviour |
|---|---|
| **Load** | The HX711 reading is corrected for boom angle (below), then compared with the safe load from the load chart. |
| **Alarm level** | 0 OK, 1 WARNING at 80 % of the safe load, 2 CRITICAL at 100 %, 3 E-STOP. A 2 % hysteresis stops flapping. Alarms only: nothing blocks motion. |
| **Load chart** | Rows of (boom angle, extension, limit kg) stored in flash. The safe load is the highest limit among rows with angle ≤ current **and** extension ≥ current; 0 kg outside the chart. A small tolerance (0.5°, 5 mm) applies at the edges. With no chart saved, a 5 kg default is used. |
| **Angle correction** | The load cell is on top of the boom with the cable over it, so its reading depends on boom angle. Hang a known weight at a few angles and record it (`CAL3 W<kg>`); the ESP32 stores `gain = measured / known` per angle (up to 8 points, interpolated) and uses `true = measured / gain`. No points recorded means no correction. |
| **Status flags** | Sent with every packet: load-cell fault, boom-sensor fault, telescope-sensor fault, no chart, outside chart, boom leaning, E-stop latched, IMU/encoder mismatch. |
| **Boom cross-check** | The IMU boom angle is compared with the boom encoder (on the pivot shaft). More than 3° apart for 0.5 s raises a mismatch flag and a CRITICAL alarm. The encoder direction is learned automatically the first time the boom moves 10° from the "Zero IMU" pose. |
| **E-stop** | `T0` latches: all motion is refused until `RST`. The Mega also disables its stepper drivers (`ESTOP_DISABLES_DRIVERS` in `mega_executor.ino`). |
| **Dead-man motion** | A motor keeps running only while its `M` command is repeated (the dashboard repeats it every 100 ms while a button or stick is held). If nothing arrives for 400 ms the axis stops, on both the Mega and the ESP32 winch. |

### FreeRTOS tasks (ESP32)

| Task | Core | Priority | Rate | Job |
|---|---|---|---|---|
| `SafetyTask` | 1 | 6 | 50 Hz | Corrected load, chart limit, load %, alarm level, status flags |
| `SensorTask` | 1 | 5 | 100 Hz | Reads all sensors into one shared struct (mutex-protected) |
| `CommandTask` | 0 | 4 | on demand | Parses G-code from USB, drives the Mega and winch, calibration |
| `TelemetryTask` | 0 | 3 | 50 Hz | Sends the `$T` packet |

### Persistent settings (ESP32 flash / NVS)

IMU zero and axis mapping, telescope scale and direction, load chart, load-cell angle corrections, boom-encoder reference and direction.

## USB protocol

Text lines at 115200 baud. Commands go PC → ESP32; every line the ESP32 sends starts with `$`.

### Commands

| Command | Meaning |
|---|---|
| `T0` | Emergency stop (latched) |
| `RST` | Clear the E-stop latch and re-enable the drivers |
| `M1`..`M4` `S<speed> D<0\|1>` | Swing / boom lift / telescope / winch. Speed 0–2500 (steps/s; scaled to PWM for the winch). Must be repeated to keep moving |
| `M0 A<axis>` | Stop one axis |
| `CAL0` | Tare the load cell (nothing hanging) |
| `CAL1 [B<0\|1> I<0\|1> T<0\|1> Q<0\|1>]` | Zero the IMU at the current pose (axis choice and inversion optional); also sets the boom-encoder reference |
| `CAL2 [S<mm/rev> I<0\|1>]` | Zero the telescope, or set its scale and direction |
| `CAL3 W<kg>` / `CAL3 CLEAR` | Record / clear a load-cell angle correction |
| `DBG` | Request a status report |
| `CFG RATE <hz>` | Telemetry rate |
| `LC UPLOAD <n>`, `LC <angle>,<ext>,<limit>`, `LC SAVE`, `LC GET` | Upload or read back the load chart |

### Packets from the ESP32

`$T` telemetry, 15 fields, 50 Hz:

```
$T,boomAngle,extensionMM,measuredLoad,actualLoad,swingAngle,ropeLenMM,fsr1,fsr2,fsr3,fsr4,boomLean,statusFlags,safeLimit,loadPct,alarmLvl
```

`measuredLoad` is the uncorrected load in kg, `actualLoad` the angle-corrected load. `loadPct` is capped at 500; **999 means the load sensor is faulty**. `statusFlags` bits are defined in `esp32_main/config.h` (`STATUS_*`) and mirrored in `dashboard/src/status.ts`.

`$D,...` lines answer `DBG` (sensor status, FSR values, telescope scale, boom cross-check, load-cell corrections) and end with `$D,END`. `$ACK,...` confirms commands and reports errors. `$LC,...` answers the load chart commands.

## Backend (FastAPI)

Binds to `127.0.0.1:8000` by default (set `SLI_API_HOST=0.0.0.0` to allow other machines; there is no authentication).

| Endpoint | Purpose |
|---|---|
| `GET /api/status`, `GET /api/ports` | Backend and serial link state; available COM ports |
| `POST /api/connect`, `POST /api/disconnect` | Open / close the serial port (a CSV session starts on connect) |
| `POST /api/command` | Send one validated G-code line |
| `POST /api/estop` | Send `T0` |
| `POST /api/calibrate/tare`, `/imu`, `/tele` | `CAL0`, `CAL1`, `CAL2` |
| `POST /api/calibrate/loadgain`, `/loadgain/clear` | `CAL3` |
| `POST /api/debug/scan` | `DBG` |
| `POST /api/loadchart/upload`, `GET /api/loadchart` | Upload a chart (succeeds only once the ESP32 confirms) / read it back |
| `GET /api/logs`, `GET /api/logs/{file}` | List / download session CSVs |
| `WS /ws/telemetry` | Pushes `telemetry`, `debug` and `ack` messages as JSON |

Interactive API docs: <http://localhost:8000/docs>. Session CSVs are written to `backend/logs/`.

## Dashboard tabs

- **Dashboard:** live telemetry, gauges, 2-D crane view, outrigger load, hold-to-move motor buttons, speed slider, Xbox controller (B = E-stop), E-stop and Reset E-stop.
- **Debug & Calibrate:** hardware status, tare, IMU zero, telescope calibration, load-cell angle correction, command log.
- **Data Logger:** live 30 s charts and saved CSV sessions.
- **Load Chart:** edit and upload the load chart, read it back from the ESP32.
- **Settings:** COM port and connection.

## Running it

**Firmware** (Arduino IDE, ESP32 board package installed). Libraries: *HX711* (bogde), *Adafruit MPU6050*, *Adafruit Unified Sensor*.

1. Open `firmware/esp32_main/esp32_main.ino`, select the ESP32 board, upload.
2. Open `firmware/mega_executor/mega_executor.ino`, select *Arduino Mega 2560*, upload.

Flash the ESP32 firmware and start the backend from the same version: the `$T` packet layout changed on 2026-09-21 and older firmware is rejected by the backend.

**Backend**

```bash
cd software/sli_desktop/backend
pip install -r requirements.txt
python main.py
```

**Dashboard**

```bash
cd software/sli_desktop/dashboard
npm install
npm run dev        # http://localhost:5173
```

Then open **Settings**, pick the ESP32's COM port and press **Connect**.

### First-time calibration order

1. Load cell: **Tare** with nothing hanging.
2. Put the boom in its zero pose, hold it still, run **Zero IMU** (this also sets the boom-encoder reference).
3. Telescope: retract, **Set Zero**, jog out, enter the measured travel and save the scale.
4. Load-cell angle correction: hang a known weight, record at 3–4 boom angles.
5. Upload the load chart in the **Load Chart** tab.

## Tests

```bash
# Backend (from software/sli_desktop/backend)
pip install -r requirements-dev.txt
python -m pytest

# Firmware logic on the PC (needs any C++ compiler: g++, clang++ or `pip install ziglang`)
python software/firmware/tests/run_tests.py
```

These test the parser, packet handling, load chart, angle correction, alarm logic and cross-check. They do not replace testing on the hardware.
