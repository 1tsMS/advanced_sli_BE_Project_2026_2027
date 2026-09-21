> **Status note (September 2026): this is the ORIGINAL design plan.** The project follows its
> three-layer idea, but the implementation differs in the places below. What is actually built is
> described in [software/software.md](../software/software.md); wiring is in
> [hardware/connections.md](../hardware/connections.md). The RTOS explanation in section 1 is still accurate.
>
> | Plan says | What was built |
> |---|---|
> | Frontend is Electron + React + Framer Motion | A plain Vite + React web app run in a browser (no Electron, no Framer Motion). Charts are simple SVG, so Recharts is not used either |
> | WebSocket on port 8765 | WebSocket and REST are both on port 8000 (`/ws/telemetry`, `/api/...`), bound to localhost by default |
> | `pyserial-asyncio` | `pyserial` read in a worker thread |
> | HX711 on GPIO 4/27, UART2 on GPIO 16/17 | HX711 on GPIO 17/16, UART2 on GPIO 23/27, MPU6050 on I2C bus 0 |
> | Tabs: Dashboard, Debug, Calibration, Data Logger, Controller, Load Chart, Settings | Dashboard (with the Xbox controller), Debug & Calibrate, Data Logger, Load Chart, Settings |
> | Load compensation `W = F / cos(θ)` (section 4) | The load cell is on top of the boom with the cable over it, so the correction is **learned**: record known weights at several boom angles (`CAL3 W<kg>`) and the ESP32 interpolates. Section 4's formula is not used |
> | `SafetyTask` with load chart, alarm levels WARN 80 % / CRITICAL 100 % | Implemented as planned, plus status flags, sensor-fault alarms and an IMU-vs-encoder boom cross-check. **Alarm only**, it never blocks motion |
> | `$T` fields 11-12 are `imuRoll`, `imuPitch` | Fields 11-12 are `boomLean` and `statusFlags`; `$T` measured load is the uncorrected load in kg |
> | Commands `T0`, `M1`-`M4`, `M0`, `CAL0-2`, `DBG`, `CFG`, `LC` | Also `RST` (clears the latched E-stop) and `CAL3`, `LC GET`. `M` commands are a dead-man: they must be repeated (every 100 ms) or the axis stops after 400 ms |
> | Firmware split into `tasks/`, `drivers/`, `protocol/`, `safety/` folders | One flat sketch folder (`esp32_main/`), as the Arduino IDE requires |
> | Phase 4: PID anti-sway | Not started |

# Advanced SLI — Software System Design Plan (v2)

## Overview

A three-layer software system for the Advanced Safe Load Indicator. This version incorporates RTOS fundamentals, boom-angle load compensation math, updated dashboard layout, and a phased firmware plan starting with bare telemetry.

---

## Table of Contents

1. [FreeRTOS Deep-Dive](#1--freertos-deep-dive-for-esp32)
2. [System Architecture](#2--system-architecture-3-layer)
3. [ESP32 Firmware Design](#3--layer-1--esp32-firmware-freertos)
4. [Load Calculation Physics](#4--load-calculation--boom-angle-compensation)
5. [Mega Executor Firmware](#5--mega-executor-firmware)
6. [Communication Protocol](#6--communication-protocol--custom-g-code)
7. [Python Middleware](#7--layer-2--python-middleware)
8. [Frontend Dashboard](#8--layer-3--frontend-electron--react)
9. [Folder Structure](#9--folder-structure)
10. [Build Phases](#10--build-phases)
11. [Open Decisions](#11--remaining-decisions)

---

## 1 — FreeRTOS Deep-Dive for ESP32

### What is RTOS and Why Do We Need It?

On a regular Arduino (or any bare-metal loop), your code runs like this:

```
void loop() {
    readSensors();     // takes ~5ms
    runSafetyCheck();  // takes ~2ms
    sendTelemetry();   // takes ~3ms (blocked waiting for Serial buffer)
    checkCommands();   // takes ~1ms
    controlMotors();   // takes ~1ms
}
// Total: ~12ms per cycle → ~83Hz max
// BUT: if sendTelemetry() blocks for 50ms (buffer full), EVERYTHING waits
```

**The problem**: Everything runs sequentially in one big loop. If one function takes longer than expected (sensor I2C timeout, serial buffer full, etc.), *everything* else gets delayed. Your safety check doesn't run. Your motor control stutters. Bad news for a crane.

**FreeRTOS solves this** by letting you split your code into **independent tasks** that run "at the same time". The ESP32 has **two CPU cores**, and FreeRTOS can actually run two tasks *truly in parallel* + rapidly switch between others.

### Core RTOS Concepts You Need

#### 1. Tasks (like threads)

A **task** is just a function that runs in its own infinite loop, independently from everything else. Each task has:
- A **priority** (higher priority = gets CPU time first)
- A **stack size** (memory allocated for its local variables)
- A **core assignment** (ESP32 has Core 0 and Core 1)

```cpp
// This is a FreeRTOS task — it runs forever, independently
void SensorTask(void *pvParameters) {
    while (true) {
        readAllSensors();
        vTaskDelay(pdMS_TO_TICKS(10));  // Sleep 10ms → runs at ~100Hz
    }
}

// Create it in setup()
void setup() {
    xTaskCreatePinnedToCore(
        SensorTask,     // function to run
        "SensorTask",   // name (for debug)
        4096,           // stack size in bytes
        NULL,           // parameter to pass
        5,              // priority (higher = more important)
        NULL,           // task handle (for controlling it later)
        1               // core to run on (0 or 1)
    );
}
```

#### 2. `vTaskDelay()` — Cooperative Sleeping

When a task calls `vTaskDelay(pdMS_TO_TICKS(10))`, it tells FreeRTOS: *"I'm done for now, wake me up in 10ms."* During those 10ms, **other tasks get to run**. This is how multiple tasks share a single CPU core.

> [!IMPORTANT]
> **Never use `delay()` in FreeRTOS!** Arduino's `delay()` blocks the entire core and starves other tasks. Always use `vTaskDelay()`.

#### 3. `vTaskDelayUntil()` — Precise Periodic Timing

`vTaskDelay()` sleeps for X ms *after* your work finishes. If your work takes 3ms and you delay 10ms, the actual period is 13ms — it drifts.

`vTaskDelayUntil()` sleeps *until* the next absolute time. If your period is 10ms and work takes 3ms, it sleeps exactly 7ms. Rock-solid frequency.

```cpp
void TelemetryTask(void *pvParameters) {
    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(20);  // 50Hz exactly

    while (true) {
        sendTelemetryPacket();
        vTaskDelayUntil(&lastWakeTime, period);  // Runs at exactly 50Hz
    }
}
```

#### 4. Shared Data Problem — Queues & Mutexes

If `SensorTask` writes to `boomAngle` and `TelemetryTask` reads it at the same time, you get **data corruption** (torn reads — half-updated values). Two solutions:

**Mutex (Mutual Exclusion Lock):**
```cpp
SemaphoreHandle_t dataMutex = xSemaphoreCreateMutex();

// In SensorTask:
xSemaphoreTake(dataMutex, portMAX_DELAY);  // Lock
sensorData.boomAngle = readBoomAngle();     // Write safely
xSemaphoreGive(dataMutex);                  // Unlock

// In TelemetryTask:
xSemaphoreTake(dataMutex, portMAX_DELAY);   // Lock
float angle = sensorData.boomAngle;          // Read safely
xSemaphoreGive(dataMutex);                   // Unlock
```

**Queue (Thread-Safe Pipe):**
```cpp
QueueHandle_t commandQueue = xQueueCreate(10, sizeof(Command));

// Frontend sends command → CommandTask receives it
// Producer:
Command cmd = parseGCode("M1 S200 D1");
xQueueSend(commandQueue, &cmd, 0);

// Consumer (MotorTask):
Command received;
if (xQueueReceive(commandQueue, &received, portMAX_DELAY)) {
    executeMotorCommand(received);
}
```

#### 5. ESP32 Dual-Core Strategy

The ESP32 has **two cores**. WiFi/BLE run on Core 0 by default. We split our tasks strategically:

```
┌─────────────────────────────────────────────────┐
│                   CORE 1                         │
│  (Dedicated to time-critical real-time work)     │
│                                                  │
│  SensorTask     [100Hz, Priority 5]              │
│  SafetyTask     [50Hz,  Priority 6 — HIGHEST]    │
│  PIDTask        [50Hz,  Priority 5]              │
│                                                  │
│  → These MUST run on time, no exceptions         │
├─────────────────────────────────────────────────┤
│                   CORE 0                         │
│  (Handles communication, non-critical work)      │
│                                                  │
│  TelemetryTask  [50Hz,  Priority 3]              │
│  CommandTask    [Event, Priority 4]              │
│  MegaBridgeTask [On-demand, Priority 3]          │
│                                                  │
│  → Important but can tolerate slight delays      │
└─────────────────────────────────────────────────┘
```

**Why this split?**
- If serial communication hiccups on Core 0, your sensor reading and safety checks on Core 1 **keep running perfectly** — they're on a completely separate CPU.
- `SafetyTask` has the highest priority on Core 1, so even if `SensorTask` is mid-read, a safety alarm will preempt it immediately.

#### 6. Task Communication Flow (Our System)

```
                        ┌──────────────┐
                        │  SensorTask  │ ← reads all hardware
                        │   (Core 1)   │
                        └──────┬───────┘
                               │ writes to shared struct (mutex-protected)
                               ▼
                     ┌────────────────────┐
                     │  SensorData struct │ ← single source of truth
                     │  (mutex-guarded)   │
                     └──┬──────┬─────┬───┘
                        │      │     │
              ┌─────────▼──┐   │   ┌─▼────────────┐
              │ SafetyTask │   │   │  PIDTask      │
              │  (Core 1)  │   │   │  (Core 1)     │
              │ checks     │   │   │ anti-sway     │
              │ load limits│   │   │ compensation  │
              └─────────┬──┘   │   └──┬────────────┘
                        │      │      │
                        ▼      ▼      ▼
              ┌─────────────────────────────┐
              │     TelemetryTask (Core 0)  │
              │  reads struct → formats $T  │
              │  → Serial.println()         │
              └─────────────────────────────┘

  PC Command ──→ Serial RX ──→ CommandTask (Core 0)
                                    │
                                    ▼ (via Queue)
                              MegaBridgeTask (Core 0)
                                    │
                                    ▼ UART2
                              Arduino Mega (stepper motors)
```

#### Summary: What You Get From RTOS

| Without RTOS (bare loop) | With FreeRTOS |
|---|---|
| One thing at a time, sequentially | Multiple things truly in parallel |
| One slow function delays everything | Tasks are independent, can't block each other |
| No priority — safety check waits for serial | Safety has highest priority, always runs first |
| Timing drifts as code grows | `vTaskDelayUntil` = rock-solid frequencies |
| Hard to add features without breaking timing | Add a new task = zero impact on existing ones |

---

## 2 — System Architecture (3-Layer)

```
┌────────────────────────────────────────────────────────────────────────┐
│                     LAYER 3 — FRONTEND (Desktop App)                   │
│               Electron + React + TypeScript + Vite                      │
│  Dark Glassmorphism Industrial UI │ WebSocket Client │ Xbox Gamepad     │
└────────────────────────────────┬───────────────────────────────────────┘
                                 │ WebSocket (ws://localhost:8765)
                                 │ REST API  (http://localhost:8000)
┌────────────────────────────────▼───────────────────────────────────────┐
│                    LAYER 2 — MIDDLEWARE (Python)                        │
│                  FastAPI + WebSocket + pyserial                         │
│     Serial Reader │ Packet Parser │ Command Router │ CSV Logger         │
└────────────────────────────────┬───────────────────────────────────────┘
                                 │ USB Serial (115200 baud)
┌────────────────────────────────▼───────────────────────────────────────┐
│                    LAYER 1 — FIRMWARE (ESP32)                           │
│                    Arduino + FreeRTOS                                   │
│  SensorTask │ SafetyTask │ PID │ TelemetryTask │ CommandTask │ Mega TX  │
│                                                                        │
│        UART2 ──→ Arduino Mega + RAMPS 1.4 (Motor Executor)             │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 3 — Layer 1 — ESP32 Firmware (FreeRTOS)

### Shared Data Struct

```cpp
struct SensorData {
    // Raw sensor values
    float boomAngleRaw;     // degrees from AS5600
    float extensionMM;      // mm from AS5600 rotation → linear conversion
    float swingAngleDeg;    // degrees from AS5600
    float loadCellRaw;      // kg from HX711 (raw, uncorrected)
    float ropeLengthMM;     // mm from N20 encoder ticks
    float imuRoll;          // degrees from MPU6050 (Madgwick-filtered)
    float imuPitch;         // degrees
    float swayAccelX;       // m/s² lateral sway
    uint16_t fsr[4];        // ADC values (0–4095) for outrigger sensors

    // Computed values
    float actualLoadKg;     // boom-angle-compensated true load
    float safeLoadLimitKg;  // from load chart at current angle + extension
    float loadPercent;      // (actualLoadKg / safeLoadLimitKg) * 100

    // Status
    uint8_t alarmLevel;     // 0=OK, 1=WARN (80%), 2=CRITICAL (100%), 3=E-STOP
    bool sensorsOK;         // all I2C devices responding
};
```

### FreeRTOS Tasks — Phased Implementation

#### Phase 1 (Week 9): Bare Bones — Just Read + Send

Only 3 tasks, no safety logic, just get data flowing end-to-end:

| Task | Core | Priority | Rate | What it does |
|---|---|---|---|---|
| `SensorTask` | Core 1 | 5 | 100Hz | Read all sensors, write to `SensorData` (mutex) |
| `TelemetryTask` | Core 0 | 3 | 50Hz | Read `SensorData` → format `$T` → Serial USB |
| `CommandTask` | Core 0 | 4 | Event | Parse incoming G-code → forward to Mega via UART2 |

#### Phase 2 (Week 11): Add Safety

| Task | Core | Priority | Rate | What it does |
|---|---|---|---|---|
| `SafetyTask` | Core 1 | **6** | 50Hz | Load chart lookup, alarm level, load% calculation |

#### Phase 3 (Week 13): Add PID Anti-Sway

| Task | Core | Priority | Rate | What it does |
|---|---|---|---|---|
| `PIDTask` | Core 1 | 5 | 50Hz | IMU sway → PID → swing motor correction |

This way we **validate the whole pipeline first** before adding complexity.

---

## 4 — Load Calculation — Boom Angle Compensation

### The Problem

The load cell is mounted at the boom tip (or on the cable/hook). As the boom tilts, the **gravitational component** acting along the load cell's axis changes.

Imagine holding a weight on a stick:
- Stick horizontal (0°) → the load cell sees maximum force (full weight hangs straight down)
- Stick at 45° → the load cell sees less, because some force is along the boom's axis (compression, not tension on the cell)
- Stick vertical (90°) → load cell sees near-zero (all force is boom compression)

### The Math

```
                         ╱ Boom (at angle θ from horizontal)
                        ╱
                       ╱  ← Load cell reads this component
                      ╱
           ┌─────────╱──────── Pivot
           │        θ
           │
           │  W (actual weight, straight down)
```

If the load cell measures force **perpendicular to the boom** (tension on cable hanging from tip):

```
F_measured = W × cos(θ)
```

Therefore the **actual load** (compensated) is:

```
W_actual = F_measured / cos(θ)
```

Where `θ` = boom angle from horizontal (from AS5600).

> [!WARNING]
> At very steep angles (θ → 90°), `cos(θ) → 0` and the division blows up. We clamp `θ` to max ~80° and flag unreliable readings above that.

### Implementation on ESP32

```cpp
// In SafetyTask (or SensorTask Phase 2):
float theta_rad = sensorData.boomAngleRaw * DEG_TO_RAD;
float cosTheta = cos(theta_rad);

if (cosTheta < 0.17) {  // ~80° — unreliable zone
    sensorData.actualLoadKg = -1;  // flag as unreliable
} else {
    sensorData.actualLoadKg = sensorData.loadCellRaw / cosTheta;
}
```

### What the Dashboard Shows

| Value | Label | Meaning |
|---|---|---|
| `loadCellRaw` | **Measured Load** | What the cell physically reads (varies with angle) |
| `actualLoadKg` | **True Load** | Angle-compensated actual weight hanging from hook |
| `safeLoadLimitKg` | **Safe Limit** | Max allowed load at current angle + extension |
| `loadPercent` | **Load %** | `(actualLoadKg / safeLoadLimitKg) * 100` |

---

## 5 — Mega Executor Firmware

Super simple — the Mega is a **dumb motor driver** that receives G-code on `Serial1` and controls steppers:

```cpp
// Mega receives via Serial1 (connected to ESP32 UART2)
// Same G-code format: M1 S200 D1, M2 S150 D0, T0, etc.
// Translates to RAMPS pin writes (step/dir/enable)
```

No RTOS needed on Mega. A simple `loop()` that:
1. Reads `Serial1` for incoming commands
2. Parses G-code
3. Sets stepper direction + step rate (timer-interrupt-based for smooth motion)
4. `T0` = immediate all-stop (disable all drivers)

---

## 6 — Communication Protocol — Custom G-code

### Telemetry: ESP32 → PC (50Hz push)

```
$T,<boomAngle>,<extensionMM>,<measuredLoad>,<actualLoad>,<swingAngle>,<ropeLenMM>,<fsr1>,<fsr2>,<fsr3>,<fsr4>,<imuRoll>,<imuPitch>,<safeLimit>,<loadPct>,<alarmLvl>\n
```

Example:
```
$T,45.20,340.0,2.45,3.21,12.5,550.0,120,115,130,118,1.2,0.4,5.0,64.2,1\n
```

- `alarmLvl`: 0=OK, 1=WARN, 2=CRITICAL, 3=ESTOP

### Commands: PC → ESP32

| Code | Parameters | Action |
|---|---|---|
| `T0` | — | **Emergency Stop** — all motors halt, alarm state |
| `M1` | `S<speed> D<dir>` | Swing (D0=CW, D1=CCW) |
| `M2` | `S<speed> D<dir>` | Boom Lift (D0=Up, D1=Down) |
| `M3` | `S<speed> D<dir>` | Telescope (D0=Extend, D1=Retract) |
| `M4` | `S<speed> D<dir>` | Winch (D0=Up, D1=Down) |
| `M0` | `A<axis>` | Stop specific axis (A1=swing, A2=lift, etc.) |
| `CAL0` | — | Tare load cell |
| `CAL1` | — | Zero IMU |
| `CAL2` | — | Reset extension encoder |
| `DBG` | — | Request I2C scan + sensor status report |
| `CFG` | `RATE <hz>` | Set telemetry rate |
| `LC` | `UPLOAD <n>` | Begin load chart upload (n = number of entries) |
| `LC` | `<angle>,<ext>,<limit>` | Single load chart entry (sent n times after UPLOAD) |
| `LC` | `SAVE` | Confirm & save load chart to ESP32 NVS flash |

### Debug Response: ESP32 → PC

When `DBG` is received, ESP responds with:
```
$D,I2C0,0x36,OK
$D,I2C1,0x36,OK
$D,I2C2,0x36,OK
$D,I2C2,0x68,OK
$D,HX711,OK
$D,FSR1,3412
$D,FSR2,3380
$D,FSR3,3401
$D,FSR4,3395
$D,END
```

### Load Chart Upload Flow

```
PC → ESP:   LC UPLOAD 5          (5 entries incoming)
PC → ESP:   LC 0,0,5.0           (angle=0°, ext=0mm, limit=5.0kg)
PC → ESP:   LC 15,0,4.5
PC → ESP:   LC 30,0,3.8
PC → ESP:   LC 45,0,2.5
PC → ESP:   LC 60,0,1.2
PC → ESP:   LC SAVE
ESP → PC:   $LC,OK,5             (saved 5 entries to NVS)
```

ESP stores the chart in NVS (Non-Volatile Storage) flash — persists across reboots. The dashboard editor just provides a nice UI to edit and push it.

---

## 7 — Layer 2 — Python Middleware

### Tech Stack
- **FastAPI** — REST + WebSocket server
- **pyserial-asyncio** — Non-blocking serial I/O
- **asyncio** — Async event loop
- **uvicorn** — ASGI server

### Module Structure (OOP)

```
sli_backend/
├── main.py                  # FastAPI app + startup
├── core/
│   ├── __init__.py
│   ├── serial_manager.py    # SerialManager: async open/close/read/write
│   ├── packet_parser.py     # PacketParser: $T CSV → TelemetryFrame dict
│   ├── command_router.py    # CommandRouter: validate + send G-code
│   ├── session_logger.py    # SessionLogger: CSV file per session
│   └── load_chart.py        # LoadChartManager: edit + upload LC commands
├── api/
│   ├── __init__.py
│   ├── ws_endpoint.py       # WebSocket /ws/telemetry broadcast
│   ├── rest_endpoints.py    # REST routes
│   └── models.py            # Pydantic models
└── config/
    ├── __init__.py
    └── settings.py          # Baud, port, log dir, telemetry rate
```

### Key REST Endpoints

| Method | Path | Purpose |
|---|---|---|
| GET | `/api/status` | Backend health + serial connection state |
| GET | `/api/ports` | List available COM ports |
| POST | `/api/connect` | Connect to a serial port |
| POST | `/api/disconnect` | Disconnect |
| POST | `/api/command` | Send raw G-code string |
| POST | `/api/calibrate/{type}` | Tare / IMU zero / encoder reset |
| POST | `/api/loadchart/upload` | Push load chart entries to ESP |
| GET | `/api/logs` | List session CSVs |

### WebSocket `/ws/telemetry`

Broadcasts parsed JSON at ~50Hz:
```json
{
  "boomAngle": 45.2,
  "extensionMM": 340.0,
  "measuredLoad": 2.45,
  "actualLoad": 3.21,
  "swingAngle": 12.5,
  "ropeLength": 550.0,
  "fsr": [120, 115, 130, 118],
  "imuRoll": 1.2,
  "imuPitch": 0.4,
  "safeLoadLimit": 5.0,
  "loadPercent": 64.2,
  "alarmLevel": 1,
  "timestamp": 1726474200.123
}
```

---

## 8 — Layer 3 — Frontend (Electron + React)

### Tech Stack
- **Electron** — Native desktop shell
- **React + TypeScript + Vite** — UI framework
- **Framer Motion** — Animations
- **Recharts** — Live line charts (30s rolling graph)
- **Canvas API** — 2D crane silhouette + gauges
- **Gamepad API** — Xbox controller (W3C standard)
- **Lucide React** — Icon library (clean, professional SVG icons)
- **WebSocket client** — Connects to Python backend

### Icon Library: Lucide React

Professional, consistent, MIT-licensed SVG icons. No emojis anywhere.

| Tab | Lucide Icon | Name |
|---|---|---|
| Dashboard | `LayoutDashboard` | Main operational view |
| Debug | `Bug` | I2C scan, raw values |
| Calibration | `SlidersHorizontal` | Tare, zero, reset |
| Data Logger | `Activity` | Session logs + live graph |
| Controller | `Gamepad2` | Xbox mapping |
| Settings | `Settings` | Port, baud, config |
| Load Chart | `Table2` | Chart editor + upload |

Other icons: `AlertTriangle` (warnings), `ShieldAlert` (critical), `ShieldCheck` (safe), `Power` (E-stop), `Plug` (connection), `Gauge` (gauges).

### Visual Design — Dark Glassmorphism + Industrial

| Element | Value |
|---|---|
| Background | `#0C0E13` with subtle dot grid pattern |
| Glass Cards | `rgba(255,255,255,0.04)`, `backdrop-filter: blur(16px)`, `border: 1px solid rgba(255,255,255,0.08)` |
| Primary Accent | Electric Cyan `#00D4FF` |
| Warning | Amber `#FFB347` |
| Danger/Alert | Crimson `#FF3B3B` |
| Safe/OK | Emerald `#00E676` |
| Typography | `Inter` (Google Fonts), `JetBrains Mono` for telemetry values |
| Sidebar | `#10131A`, active tab = left cyan border accent, icon + label |
| Gauges | Circular arc gauges with glow effect on needle |

### Dashboard Tab — Updated Layout

```
┌──────────────────────────────────────────────────────────────────────────────────┐
│  ⬛ E-STOP   🟢 COM4 Connected   115200 baud           ⚠ LOAD: 64%  WARNING    │
├──────────────┬────────────────────────────┬──────────────────────────────────────┤
│              │                            │                                      │
│  TELEMETRY   │     MOTOR CONTROLS         │   2D CRANE VISUALISATION             │
│  PANELS      │                            │   (Boom silhouette + angle arc)      │
│  (glass      │   ┌────┐ ┌────┐           │   (Green/Amber/Red envelope zones)   │
│   cards)     │   │ SW◄│ │ SW►│  Swing    │   (Hook animation)                   │
│              │   └────┘ └────┘           │                                      │
│  Boom Angle  │   ┌────┐ ┌────┐           │   ┌──────────┐  ┌──────────┐         │
│  Extension   │   │ LF▲│ │ LF▼│  Lift    │   │  GAUGE   │  │  GAUGE   │         │
│  Swing Angle │   └────┘ └────┘           │   │ Load %   │  │ Boom ∠   │         │
│  Measured Ld │   ┌────┐ ┌────┐           │   └──────────┘  └──────────┘         │
│  True Load   │   │ TE+│ │ TE-│  Extend  │         ┌──────────┐                  │
│  Safe Limit  │   └────┘ └────┘           │         │  GAUGE   │                  │
│  Load %      │   ┌────┐ ┌────┐           │         │ Swing ∠  │                  │
│  Rope Len    │   │ WN▲│ │ WN▼│  Winch   │         └──────────┘                  │
│  IMU Roll    │   └────┘ └────┘           ├──────────────────────────────────────┤
│  IMU Pitch   │                            │   OUTRIGGER FSR VIEW (Overhead)      │
│              │   SPEED: [====●════]       │   [FL●] ─────────── [FR●]            │
│              │                            │    |                   |             │
│              │   XBOX: 🎮 Connected       │    |     CRANE BASE   |             │
│              │   LStick: ██░░ RStick: █░░ │    |                   |             │
│              │                            │   [RL●] ─────────── [RR●]            │
└──────────────┴────────────────────────────┴──────────────────────────────────────┘
```

**Layout columns: 25% telemetry | 30% controls | 45% visualization**

### Gauges (3 circular arc gauges)

1. **Load %** — 0–120% range, color changes: green < 60%, amber 60–80%, red 80–100%, flashing red > 100%
2. **Boom Angle** — 0–90° range, shows current boom angle, cyan accent
3. **Swing Angle** — -180° to +180°, shows crane rotation

Gauge style: dark background circular arc, glowing needle, value in center with `JetBrains Mono` font.

### Hold-to-Move Buttons

```
onPointerDown → send "M1 S<speed> D0" (start moving)
  ↓ (button held)
onPointerUp   → send "M0 A1"           (stop that axis)
```

Same for keyboard (WASD + QE), and Xbox controller axes.

### Xbox Controller (Gamepad API)

Polling via `requestAnimationFrame` loop:
- **Left Stick X** → Swing (M1)
- **Left Stick Y** → Boom Lift (M2)
- **Right Stick Y** → Telescope (M3)
- **Right Trigger** → Winch Down (M4 D1)
- **Left Trigger** → Winch Up (M4 D0)
- **B Button** → Emergency Stop (T0)

Deadzone: 0.15 (configurable in Controller tab). Raw axis value (0.0–1.0) → mapped to speed (0–255) → sent as G-code to Python → Serial → ESP.

**No filtering, no safety logic in frontend.** ESP is the brain.

### Left Sidebar Tabs

```
┌──────────────────┐
│ ◻ SLI Dashboard  │  ← App title / logo
├──────────────────┤
│ ▦  Dashboard     │  ← Main view (default)
│ 🪲 Debug         │
│ ⊞  Calibration   │
│ 📊 Data Logger   │
│ 🎮 Controller    │
│ ⊞  Load Chart    │
│ ⚙  Settings      │
├──────────────────┤
│ COM4 ● Connected │  ← Status footer
└──────────────────┘

(Icons shown as emoji for readability — actual implementation uses Lucide SVG icons)
```

### Debug Tab

```
┌──────────────────────────────────────────────────────────────────┐
│  I2C BUS SCAN                          RAW SENSOR VALUES         │
│  ┌────────────────────────────────┐    ┌────────────────────────┐│
│  │ Bus 0 (GPIO21/22)             │    │ AS5600 Swing: 2341     ││
│  │   0x36 AS5600 Swing    ● OK   │    │ AS5600 Boom:  1023     ││
│  │                               │    │ AS5600 Tele:  3891     ││
│  │ Bus 1 (GPIO25/26)             │    │ MPU6050 AccX: -0.032   ││
│  │   0x36 AS5600 Boom     ● OK   │    │ MPU6050 AccY:  0.981   ││
│  │                               │    │ HX711 Raw:    -234521  ││
│  │ Bus 2 (GPIO32/33)             │    │ FSR1 ADC:     3412     ││
│  │   0x36 AS5600 Tele     ● OK   │    │ FSR2 ADC:     3380     ││
│  │   0x68 MPU6050         ● OK   │    │ FSR3 ADC:     3401     ││
│  │                               │    │ FSR4 ADC:     3395     ││
│  │ HX711 (GPIO4/27)       ● OK   │    │ N20 Enc Ticks: 4210   ││
│  │ FSR ADC (GPIO34-39)    ● OK   │    └────────────────────────┘│
│  └────────────────────────────────┘                              │
│  [🔄 Refresh Scan]                                               │
└──────────────────────────────────────────────────────────────────┘
```

---

## 9 — Folder Structure

```
advanced_sli_BE_Project_2026_2027/
├── software/
│   ├── test_codes/                  # ← PRESERVED — existing test sketches
│   │   ├── 3_AS5600_test/
│   │   ├── FSR_test/
│   │   ├── RAMPS_test/
│   │   ├── conn_test/
│   │   ├── esp_serial_test/
│   │   ├── hx_711_test/
│   │   ├── mega_serial_test/
│   │   ├── mpu_hx711_test/
│   │   └── n20_test/
│   │
│   ├── firmware/                    # ← NEW — embedded firmware
│   │   ├── esp32_main/              # Main ESP32 FreeRTOS firmware
│   │   │   ├── esp32_main.ino       # Setup + task creation
│   │   │   ├── tasks/
│   │   │   │   ├── sensor_task.h/.cpp
│   │   │   │   ├── telemetry_task.h/.cpp
│   │   │   │   ├── command_task.h/.cpp
│   │   │   │   ├── safety_task.h/.cpp     (Phase 3)
│   │   │   │   └── pid_task.h/.cpp        (Phase 4)
│   │   │   ├── drivers/
│   │   │   │   ├── as5600_driver.h/.cpp
│   │   │   │   ├── mpu6050_driver.h/.cpp
│   │   │   │   ├── hx711_driver.h/.cpp
│   │   │   │   ├── fsr_reader.h/.cpp
│   │   │   │   └── n20_encoder.h/.cpp
│   │   │   ├── protocol/
│   │   │   │   ├── gcode_parser.h/.cpp
│   │   │   │   ├── telemetry_formatter.h/.cpp
│   │   │   │   └── mega_bridge.h/.cpp
│   │   │   ├── safety/
│   │   │   │   ├── load_chart.h/.cpp      (Phase 3)
│   │   │   │   └── load_calculator.h/.cpp (Phase 3)
│   │   │   └── config.h                   # Pin defs, I2C addrs, rates
│   │   │
│   │   └── mega_executor/               # Arduino Mega stepper driver
│   │       ├── mega_executor.ino
│   │       ├── gcode_parser.h/.cpp
│   │       └── stepper_controller.h/.cpp
│   │
│   └── sli_desktop/                 # ← NEW — PC software (all layers)
│       ├── backend/                 # Python FastAPI middleware
│       │   ├── sli_backend/
│       │   │   ├── core/
│       │   │   ├── api/
│       │   │   └── config/
│       │   ├── main.py
│       │   └── requirements.txt
│       │
│       └── dashboard/               # Electron + React frontend
│           ├── electron/
│           ├── src/
│           │   ├── components/
│           │   │   ├── panels/      # Telemetry cards
│           │   │   ├── crane2d/     # Canvas crane renderer
│           │   │   ├── gauges/      # Circular arc gauge components
│           │   │   ├── controls/    # Motor buttons, E-stop
│           │   │   ├── outrigger/   # FSR 4-corner view
│           │   │   └── tabs/        # Tab page layouts
│           │   ├── hooks/           # useWebSocket, useGamepad, useTelemetry
│           │   ├── types/           # TypeScript interfaces
│           │   └── App.tsx
│           ├── package.json
│           └── vite.config.ts
│
├── hardware/
│   ├── cad_model/
│   └── connections.md
├── docs/
├── images/
├── reference/
└── README.md
```

> [!NOTE]
> `software/test_codes/` is **completely untouched**. All new firmware goes under `software/firmware/` and all PC software goes under `software/sli_desktop/`.

---

## 10 — Build Phases

### Phase 1 — "It Talks"

**Goal**: Raw sensor data flows from ESP32 → Python → Dashboard screen. No safety, no calculation, just prove the entire pipeline works end-to-end.

**Firmware (ESP32)**:
- [ ] FreeRTOS skeleton with `SensorTask`, `TelemetryTask`, `CommandTask`
- [ ] Basic sensor drivers (AS5600 x3, MPU6050, HX711, FSR, N20 enc)
- [ ] `$T` telemetry packet at 50Hz
- [ ] G-code command parser (M1-M4, T0, CAL0-2, DBG)
- [ ] UART2 bridge to Mega

**Firmware (Mega)**:
- [ ] G-code parser on Serial1
- [ ] Stepper control (step/dir via RAMPS pins)
- [ ] T0 emergency stop

**Python Backend**:
- [ ] Serial connection manager
- [ ] `$T` packet parser
- [ ] WebSocket broadcast
- [ ] REST endpoints (connect, disconnect, command, ports)

**Frontend**:
- [ ] Electron + React + Vite scaffold
- [ ] Sidebar navigation
- [ ] Dashboard: telemetry panels (raw number display)
- [ ] Motor control buttons (hold-to-move)
- [ ] E-stop button (always visible in header bar)
- [ ] Settings tab: COM port selection + connect/disconnect

### Phase 2 — "It Looks Good & It's Fun"

**Goal**: Full visual polish, all tabs functional, controller support, data logging — the complete operational UI.

**Dashboard Visuals**:
- [ ] 2D crane Canvas renderer (boom angle + extension + hook)
- [ ] 3 circular arc gauges (Load %, Boom Angle, Swing Angle)
- [ ] Overhead FSR outrigger view
- [ ] Full glassmorphism styling + animations
- [ ] Safety status bar in header (color-coded load %)

**Tabs & Features**:
- [ ] Debug tab (I2C scan display from `$D` packets)
- [ ] Xbox controller integration (Gamepad API)
- [ ] Controller tab (button mapping + live axis display)
- [ ] Calibration tab (tare, IMU zero, encoder reset)
- [ ] CSV session logger (Python backend)
- [ ] Data Logger tab (session file list + rolling 30s chart)

### Phase 3 — "It's Smart"

**Goal**: Load calculation, safety limits, and load chart management — the actual SLI brain.

- [ ] ESP32: Load calculation (boom angle compensation)
- [ ] ESP32: `SafetyTask` with load chart lookup
- [ ] ESP32: Alarm levels (OK → WARN → CRITICAL)
- [ ] Load chart editor tab in dashboard
- [ ] Load chart upload flow (LC commands)
- [ ] Both measured + true load displayed in dashboard

### Phase 4 — "It Prevents Disaster"

**Goal**: Active sway prevention and alarm systems.

- [ ] ESP32: PID anti-sway task (IMU feedback → swing correction)
- [ ] Sway indicator in dashboard
- [ ] Audio alarm in frontend (browser Audio API)

### Future

- 3D crane visualization (Three.js — swap Canvas renderer component)
- AI operator scoring (LSTM on logged CSV data)
- Real crane sensor integration
- WiFi telemetry option

---

## 11 — Remaining Decisions

> [!NOTE]
> **Load cell mounting**: Is the load cell measuring cable tension at the boom tip, or is it inline with the hook/cable? This affects the angle-compensation formula. `F = W × cos(θ)` assumes the cell measures perpendicular force at the tip. If it's inline with the cable, the formula changes slightly.

> [!NOTE]
> **Stepper speed control on Mega**: Using timer interrupts (AccelStepper library) vs. direct `delayMicroseconds` stepping. AccelStepper gives acceleration/deceleration profiles which would feel smoother. Worth the complexity?

> [!NOTE]
> **Telemetry start trigger**: Should the ESP32 start pushing telemetry immediately on boot, or wait until the PC sends a `CFG START` command? Auto-start is simpler but could flood an unlistening port.
