# BE Capstone Project

## Project Title

**Advanced SLI**

---

## Team Details

| Sr. No. | Name of Student | Roll No. | Branch | Email ID |
|---|---|---|---|---|
| 1 | Mohammed Salah Altaf Chogle | 41 | Automation & Robotics | 2023.mohammed.chogle@ves.ac.in |
| 2 | Ali Kalsekar | 53 | Automation & Robotics | d2024.ali.kalsekar@ves.ac.in |
| 3 | Aayush Kadam | 51 | Automation & Robotics | 2023.aayush.kadam@ves.ac.in |
| 4 |  |  |  |  |

---

## Guide Details

**Project Guide:** Gopalkrishnan Narayanan 
**Department:** Automation and Robotics  
**Institute:** VESIT, Mumbai  

---

## Problem Statement

> The aim of this project is to design and develop an affordable, retrofittable Safe Load Indicator (SLI) system for low-capacity truck-mounted pick-and-carry mobile cranes by using multi-sensor feedback (load, angle, extension, inertial data) and embedded control technology, addressing the lack of accurate, low-cost load-monitoring and safety systems available for this crane category.

---

## Abstract

Low-capacity truck-mounted pick-and-carry hydraulic cranes, commonly used at local worksites, largely lack accurate and affordable Safe Load Indicator (SLI) systems, unlike larger construction-grade cranes which have factory-fitted rated capacity indicators. Operators of these smaller cranes often rely on experience alone, increasing the risk of overloading and tipping incidents. This project proposes the design and development of an Advanced Intelligent Safe Load Indicator (AI-SLI): a sensor-driven system that monitors boom angle, extension, hydraulic pressure (as a proxy for load), and inertial motion data to provide real-time load percentage, safety alarms, and operational analytics.

The project is being developed in two parallel tracks: a custom-built scale-model truck-mounted crane platform (with independently actuated swing, telescopic extension, boom-lift, and cable-reeling axes, each with encoder and sensor feedback) used for mechanism validation and algorithm development, and eventual sensor integration on a real crane for field validation. The scale model itself is a standalone multi-axis robotic-arm project, combining mechanical design, embedded systems, and sensor fusion.

Software development follows a phased approach: starting with basic rated-capacity load-chart safety limits, followed by PID-based anti-sway control, tipping prediction using load and outrigger data, and eventually AI-based analytics such as inertial-data-driven operator scoring. The expected outcome is a working scale-model demonstrator validating the sensing and control approach, laying the foundation for deployment on a real crane.

---

## Objectives

1. To study existing Safe Load Indicator systems and identify gaps in affordability and availability for low-capacity truck-mounted cranes.
2. To design and build a multi-axis scale-model crane (swing, telescopic extension, boom lift, cable reeling) as a development and validation platform.
3. To design a sensor and control architecture capable of measuring load, angle, extension, and inertial motion.
4. To implement basic rated-capacity load-chart safety limit functionality.
5. To progressively implement advanced features: anti-sway control (PID) and tipping prediction.
6. To validate the developed system and, in later stages, apply learnings toward real crane sensor integration.

---

## Scope of the Project

- Design and fabrication of a multi-axis scale-model truck-mounted crane (mechanical + electrical)
- Sensor integration: load cell, IMU, magnetic encoders, limit switches
- Embedded firmware for motor control and sensor data acquisition
- Desktop dashboard application for live monitoring, data logging, and analytics
- Phased safety software: load-chart limits → anti-sway control → tipping prediction
- Data collection and testing on the scale model
- Preliminary exploration of real crane sensor integration (later phase, scope to be expanded as work progresses)

---

## Existing System

Large construction-grade cranes typically come with factory-fitted Rated Capacity Indicator (RCI) / SLI systems. However, small, local, truck-mounted pick-and-carry hydraulic cranes commonly used at smaller worksites usually operate without such systems.

Limitations of the current situation:

- High cost of commercial SLI systems relative to the value of low-capacity cranes
- Lack of retrofittable options for older or lower-capacity crane models
- Operators rely on experience/judgement rather than real-time load feedback
- No predictive safety features (e.g., sway or tipping warnings) on this crane category
- Limited accessibility of safety technology for smaller crane operators

---

## Proposed System

**Main idea:** An affordable, sensor-driven Safe Load Indicator system, developed and validated on a custom-built scale-model crane before targeting real crane deployment.

**How it works:** Boom angle, extension length, and load/pressure are continuously measured and combined (via a load-chart lookup) to compute real-time load percentage and trigger safety alarms. Inertial (IMU) data is used to detect motion events, assist with anti-sway damping, and support operator analytics. Data is transmitted to a desktop dashboard for live monitoring and logging.

**Major components:** Multi-axis scale-model crane (swing, telescopic extension, boom lift, cable reeling), magnetic encoders, IMU, load cell, microcontroller-based control system, and a web-based dashboard on the PC (React frontend, Python FastAPI backend).

**Expected benefits:** Real-time load awareness, improved operator safety, a low-cost architecture suited to smaller cranes, and a foundation for future predictive safety features.

**Video** https://www.youtube.com/watch?v=u0mv6QGTxTg

---

## System Architecture

```text
Dashboard (React, browser) --REST + WebSocket :8000--> Backend (FastAPI) --USB 115200--> ESP32 --UART 9600--> Mega + RAMPS
                                                                                          |                       |
                                                              sensors: 3 x AS5600, MPU6050, HX711, 4 x FSR, N20   3 x NEMA17
```

The ESP32 makes every safety decision; the dashboard only displays and sends commands. The control flow of the firmware is shown in the flowchart below.

---

## Hardware Requirements

| Sr. No. | Component | Specification | Quantity | Purpose |
| ------- | --------- | ------------- | -------- | ------- |
| 1       | NEMA17 Stepper Motor | 17HS8401 | 3 | Swing, boom lift, telescopic extension |
| 2       | N20 DC Gear Motor | 150 RPM, encoder-integrated | 1 | Cable reeling / winch drive |
| 3       | ESP32 (30-pin) | — | 1 | Main controller: sensors, safety logic, PC link |
| 4       | Arduino Mega 2560 | — | 1 | Stepper motor executor |
| 5       | RAMPS 1.4 | — | 1 | Stepper driver shield |
| 6       | A4988 Stepper Driver | — | 4 (3 used) | Stepper motor driving |
| 7       | DRV8833 | — | 1 | N20 winch motor driver |
| 8       | AS5600 Magnetic Encoder | — | 3 | Swing, boom and telescope position feedback |
| 9       | MPU6050 IMU | 6-axis | 1 | Boom angle and boom lean |
| 10      | HX711 + Load Cell | 5 kg | 1 | Load sensing |
| 11      | FSR | RP-C18.3-ST | 4 | Outrigger load |
| 12      | Limit Switches | Mechanical | — | Axis end-stop safety (**not fitted yet**) |
| 13      | 12V Power Supply | 12V, 10A | 1 | System power |

Wiring is in [hardware/connections.md](hardware/connections.md); more detail in [hardware/hardware.md](hardware/hardware.md).

---

## Software Requirements

| Sr. No. | Software / Tool | Version | Purpose |
| ------- | --------------- | ------- | ------- |
| 1       | Arduino IDE + ESP32 board package | — | Firmware for the ESP32 and the Arduino Mega |
| 2       | Arduino libraries: HX711 (bogde), Adafruit MPU6050, Adafruit Unified Sensor | — | Sensor drivers |
| 3       | Fusion 360 | — | Mechanical CAD design |
| 4       | Python 3 with FastAPI, uvicorn, pyserial, pydantic | 3.10+ | Backend: serial link, REST/WebSocket API, CSV logging |
| 5       | Node.js + npm (React, Vite, TypeScript, Lucide) | React 19, Vite 8 | Web dashboard |
| 6       | pytest, httpx | — | Backend tests |
| 7       | A C++ compiler (g++, clang++ or `pip install ziglang`) | — | Firmware unit tests on the PC |

---

## Technologies Used

* Embedded C++ (Arduino) with FreeRTOS on the ESP32
* Python (FastAPI backend, pyserial, CSV logging)
* TypeScript, React and Vite (web dashboard)
* Arduino Mega, RAMPS 1.4, A4988 stepper drivers; DRV8833 for the winch
* Sensor fusion: complementary filter for the IMU; IMU-versus-encoder cross-check for the boom angle
* Machine Learning (LSTM: planned for the future tipping-prediction / operator-scoring phase)
* CAD Design (Fusion 360), 3D printing and laser cutting

---

## Methodology

1. Literature survey
2. Problem identification
3. Requirement analysis
4. System design
5. Hardware/software development
6. Integration
7. Testing and validation
8. Documentation and publication

---

## Project Timeline

| Week / Month | Task Planned          | Status                            |
| ------------ | --------------------- | --------------------------------- |
| Week 1       | Problem finalization  | Completed
| Week 2       | Literature survey     | Completed                                  |
| Week 3       | Requirement analysis  | Completed                                  |
| Week 4       | System design         | Completed                                  |
| Week 5       | Prototype development | In Progress                                  |
| Week 6       | Testing               | Pending                                  |
| Week 7       | Documentation         | Pending                                  |
| Week 8       | Paper writing         | Pending                                  |

---

## Weekly Progress Updates

Students must update this section every week.

| Week   | Date | Work Completed | Work Planned for Next Week | Issues / Challenges | GitHub Commit Link |
| ------ | ---- | -------------- | -------------------------- | ------------------- | ------------------ |
| Week 1 | Feb 2026     | Finalized problem statement               |    Begin literature survey	                        | None recorded | [ce85a47](https://github.com/1tsMS/advanced-sli-crane-system-personal-repo/commit/ce85a47) |
| Week 2 | Mar 2026     | Literature survey               | Incorporate review feedback, finalize scope                           | None recorded | [8b99da1](https://github.com/1tsMS/advanced-sli-crane-system-personal-repo/commit/8b99da1) |
| Week 3 | Apr 2026     | Incorporated review feedback/modifications; finalized scope for submission               | Requirement analysis & component selection                           | None recorded | [01350b8](https://github.com/1tsMS/advanced-sli-crane-system-personal-repo/commit/01350b8) |
| Week 4 | Jul 2026     | Requirement analysis & component selection               | Research mechanical systems for the prototype design | None recorded | No dedicated commit (planning/discussion with guide, not tracked in repo) |
| Week 5 | Jul 2026     | Researched mechanical systems for prototype design               | Begin mechanical design & 3D CAD modelling | None recorded | No dedicated commit (research phase, no artifact yet) |
| Week 6 | Jul 2026     | Mechanical design & 3D CAD modelling               | 3D print mechanical parts and begin assembly | None recorded | [bc0823e](https://github.com/1tsMS/advanced-sli-crane-system-personal-repo/commit/bc0823e), [a0f5018](https://github.com/1tsMS/advanced-sli-crane-system-personal-repo/commit/a0f5018) |
| Week 7 | Aug 2026     | 3D printing of mechanical parts and assembly               | Prototype PCB and electronic configuration | None recorded | [dad9858](https://github.com/1tsMS/advanced-sli-crane-system-personal-repo/commit/dad9858) |
| Week 8 | Aug 2026     | Prototype PCB and electronic configuration               | Prototype testing | None recorded | [9e6e136](https://github.com/1tsMS/advanced-sli-crane-system-personal-repo/commit/9e6e136), [2b6f23e](https://github.com/1tsMS/advanced-sli-crane-system-personal-repo/commit/2b6f23e) |
| Week 8 | Aug 2026     | Prototype testing done             | issues in model noted and updated laser cut files                           | Mechanical fit issues found in the model during assembly, addressed by revising the laser-cut base plate | [8bd8747](https://github.com/1tsMS/advanced-sli-crane-system-personal-repo/commit/8bd8747) |
| Week 9 | Sep 2026     | Sensor integration: stand-alone test sketches for the 3 AS5600, MPU6050, HX711 load cell, 4 FSR, N20 motor + encoder and the ESP32-to-Mega serial link | Sensor integration                           | None recorded | [c3c4124](https://github.com/1tsMS/advanced-sli-crane-system-personal-repo/commit/c3c4124), [122a4d4](https://github.com/1tsMS/advanced-sli-crane-system-personal-repo/commit/122a4d4) |
| Week 10 | Sep 2026     | Firmware development: ESP32 FreeRTOS firmware (sensor, telemetry and command tasks), Mega stepper executor, FastAPI backend and React dashboard (data pipeline, motor controls, debug and calibration tab) | Firmware development                           | Telescope encoder jumpy; N20 winch runs one way only | [584ee0f](https://github.com/1tsMS/advanced-sli-crane-system-personal-repo/commit/584ee0f), [2364f7b](https://github.com/1tsMS/advanced-sli-crane-system-personal-repo/commit/2364f7b) |
| Week 11 | Sep 2026     | Safety features written and tested on a PC: latched E-stop, dead-man motion stop, load chart stored in flash, SafetyTask (load-chart limit, alarms, sensor-fault and IMU/encoder checks), load-cell angle correction, Data Logger and Load Chart tabs, automated tests. Not yet run on the crane | Calibration & implement basic load-chart safety limits                           | Hardware calibration and testing still to do | [4d46b97](https://github.com/1tsMS/advanced_sli_BE_Project_2026_2027/commit/4d46b971c67dc9eb7af8a022edf3d1ba7c1e6e89) (branch `latest_release_21_9_26`) |
| Week 12 | Sep 2026     | Flashed the Week 11 safety firmware onto the ESP32 and Mega for the first time; ran the backend + dashboard against the real crane over 5 sessions (~103k telemetry rows logged to CSV) | IMU zero + boom-encoder reference calibration on the physical crane; two-point load cell calibration; harden the backend against a serial-port disconnect instead of crashing | Uncalibrated IMU produced boom angles from -117° to +199°; `BOOM_MISMATCH` fired on most rows as a result; one session had the boom IMU undetected for its full duration; raw load cell spiked to 44.76 kg (above the 10 kg valid ceiling) before taring; backend crashed once on a Windows `PermissionError` when the ESP32's COM port dropped | (not yet committed) |
| Week 13 | Oct 2026     |                | 	Implement PID-based anti-sway control; begin tipping-prediction groundwork                           |                     |                    |
| Week 14 | Oct 2026     |                | 	Full system testing, bug fixes, results compilation, documentation                           |                     |                    |
| Week 15 | Oct 2026     |                | Research paper writing                          |                     |                    |
---

## Design Files

Upload and link all design files here.

| File Type       | File Name / Link | Description |
| --------------- | ---------------- | ----------- |
| CAD Model       | [hardware/cad_model/V1](hardware/cad_model/V1) | Fusion 360 export of the scale-model crane: STL parts, 3MF assembly, DXF files for laser cutting |
| Circuit Diagram | Not added yet (wiring tables: [hardware/connections.md](hardware/connections.md)) | Pin-by-pin connections for the ESP32, Mega + RAMPS and all sensors |
| PCB Design      | Not added yet (prototype protoboard) | |
| Flowchart       | [images/system_architecture.png](images/system_architecture.png) | Control flowchart of the SLI logic |
| Simulation File | None (PC unit tests: [software/firmware/tests](software/firmware/tests)) | Firmware logic tested on the PC |

---

## Circuit Diagram

A drawn circuit diagram has not been added yet. The complete pin-by-pin wiring (three I2C buses, HX711, FSRs,
winch driver, ESP32-to-Mega link, RAMPS stepper slots) is in [hardware/connections.md](hardware/connections.md).

```markdown
![Circuit Diagram](images/circuit_diagram.png)
```

---

## Flowchart / Algorithm

![Flowchart](images/system_architecture.png)

### Algorithm (as implemented in the ESP32 firmware)

1. **Start-up:** initialise the three I2C buses and all sensors, calibrate the gyro, and restore the saved settings from flash (IMU zero, telescope scale, load chart, load-cell angle corrections).
2. **Every 10 ms (SensorTask):** read the encoders, IMU, load cell, outrigger FSRs and winch encoder into one shared data structure.
3. **Every 20 ms (SafetyTask, highest priority):**
   1. Correct the load cell reading for the boom angle (learned per-angle table).
   2. Look up the safe load for the current boom angle and extension in the load chart (0 kg outside the chart; a 5 kg default if no chart is saved).
   3. Compute load % and the alarm level: 80 % warning, 100 % critical, with hysteresis.
   4. Check sensor health and that the IMU and boom encoder agree; a fault raises a critical alarm and a status flag.
   5. A latched E-stop overrides everything.
4. **Every 20 ms (TelemetryTask):** send the telemetry packet to the PC.
5. **On demand (CommandTask):** parse commands from the PC. Motion commands must be repeated while a control is held, otherwise the axis stops after 400 ms; `T0` latches an emergency stop until `RST`.
6. **PC:** the backend logs every packet to a CSV file and forwards it to the dashboard, which shows the load, alarms and status.

The safety logic only raises alarms; it never blocks motion.

---

## Implementation Details

### Hardware Implementation

A 3D-printed scale-model truck-mounted crane with four axes: swing, boom lift and telescopic extension (NEMA17 steppers driven through a Mega + RAMPS 1.4) and a cable winch (N20 gear motor through a DRV8833, driven directly by the ESP32). Position feedback comes from three AS5600 magnetic encoders on separate I2C buses; an MPU6050 on the boom gives the boom angle and lean; a 5 kg load cell (HX711) on top of the boom, with the cable running over it, measures the load; four FSRs under the outriggers measure their load. The 12 V supply powers the motors; the ESP32 runs from USB. See [hardware/hardware.md](hardware/hardware.md) and [hardware/connections.md](hardware/connections.md).

### Software Implementation

- **ESP32 firmware (C++, FreeRTOS):** sensor, safety, telemetry and command tasks; custom drivers for the three AS5600s (including a bit-banged I2C bus), a non-blocking HX711 driver; settings stored in flash; a text G-code protocol over USB serial.
- **Mega firmware:** steps the three stepper axes; stops on E-stop and when commands stop arriving.
- **Backend (Python, FastAPI):** validates commands, parses the ESP32's packets, serves REST and WebSocket, logs every session to CSV.
- **Dashboard (React, TypeScript, Vite):** live telemetry, gauges, crane view, hold-to-move controls and Xbox controller, calibration tools, load chart editor, data logger.

Details, the command and packet formats and the API are in [software/software.md](software/software.md).

---

## Code Structure

```text
advanced-sli-crane-system-personal-repo/
├── README.md
├── docs/
│   ├── literature_survey.md
│   ├── software_design_plan.md      original design plan (see its status note)
│   ├── project_status_report.md
│   └── project_checklist.md         live to-do list
├── hardware/
│   ├── connections.md              wiring tables
│   ├── hardware.md
│   └── cad_model/V1/               STL / 3MF / DXF files
├── software/
│   ├── software.md
│   ├── firmware/
│   │   ├── esp32_main/             ESP32 firmware
│   │   ├── mega_executor/          Arduino Mega firmware
│   │   └── tests/                  PC unit tests for the firmware logic
│   ├── sli_desktop/
│   │   ├── backend/                FastAPI backend (+ tests/)
│   │   └── dashboard/              React dashboard
│   └── test_codes/                 stand-alone hardware test sketches
├── images/
└── reference/
```

---

## How to Run the Project

The full guide, including the calibration order, is in [software/software.md](software/software.md). In short:

### Step 1: Clone the Repository

```bash
git clone https://github.com/1tsMS/advanced-sli-crane-system-personal-repo.git
```

### Step 2: Flash the firmware

In the Arduino IDE (with the ESP32 board package and the HX711, Adafruit MPU6050 and Adafruit Unified Sensor libraries), upload `software/firmware/esp32_main/esp32_main.ino` to the ESP32 and `software/firmware/mega_executor/mega_executor.ino` to the Mega 2560.

### Step 3: Start the backend

```bash
cd software/sli_desktop/backend
pip install -r requirements.txt
python main.py
```

### Step 4: Start the dashboard

```bash
cd software/sli_desktop/dashboard
npm install
npm run dev
```

Open <http://localhost:5173>, go to **Settings**, pick the ESP32's COM port and press **Connect**.

### Step 5: Observe the output

Live boom angle, extension, load, safe limit, load % and alarm state appear on the dashboard, and every session is saved as a CSV in `software/sli_desktop/backend/logs/`.

---

## Testing and Results

Status as of 21 September 2026. Tests on the crane itself are still to be done; the results below are marked accordingly.

| Test No. | Test Description | Expected Result | Actual Result | Status      |
| -------- | ---------------- | --------------- | ------------- | ----------- |
| 1        | Firmware logic on a PC: G-code parser, load chart, angle correction, alarm levels, sensor-fault flags, IMU/encoder cross-check, packet format (`software/firmware/tests`) | All checks pass | 221 checks passed; all firmware sources compile | Pass (PC only) |
| 2        | Backend on a PC: packet parser, command validation, load chart upload against a simulated ESP32, CSV logger, REST API (`backend/tests`) | All tests pass | 84 tests passed | Pass (PC only) |
| 3        | Dashboard against a simulated ESP32: load chart upload and read-back, load-cell angle correction, fault displays, Xbox controller commands | Correct behaviour in the browser | Behaved as designed | Pass (simulated) |
| 4        | Data pipeline on the hardware: ESP32 telemetry to the dashboard, hold-to-move motor control, sensor reading | Live data and motion | Working (per the 20 Sept status report) | Pass |
| 5        | E-stop latch, dead-man stop, fault alarms, load chart and SafetyTask on the hardware | As designed | Not yet run on the crane | Pending |
| 6        | Load cell tare, two-point calibration and angle correction with known weights | Load within tolerance at all boom angles | Not yet done | Pending |
| 7        | Telescope encoder accuracy after the mechanical fix | Extension within a few mm of a ruler | Known issue: jumpy | Pending |

---

## Result Images / Videos

Not added yet. To be added after hardware testing (prototype photo, dashboard screenshots with real data, test results).

A project video is linked under *Proposed System*.

---

## Applications

1. Retrofit Safe Load Indicator for small truck-mounted pick-and-carry cranes that have none.
2. A test platform for crane safety algorithms (load charts, anti-sway control, tipping prediction) before trying them on a real crane.
3. Recording of lifts (angle, extension, load, alarms) for operator training and incident review.
4. A base for a real-crane rated capacity indicator that uses hydraulic pressure as a load proxy.

---

## Advantages

1. Low cost: common hobby-grade sensors and controllers, no proprietary hardware.
2. Safety decisions are made on the ESP32, so they do not depend on the PC or the dashboard.
3. Fail-safe behaviour: latched E-stop, motion that stops when commands stop arriving, and alarms for a dead load cell, boom sensor or telescope sensor.
4. A conservative load chart lookup (never rounds the limit up) with an editable chart stored in flash.
5. The load-cell correction is learned from known weights, so it works whatever the sensor's mounting.
6. Every session is logged to CSV; the whole system has automated tests.

---

## Limitations

1. Validated only on a 5 kg scale model; nothing has been tried on a real crane.
2. The safety logic raises alarms only; it does not stop or restrict motion.
3. The newest safety features have been tested on a PC but not yet on the hardware.
4. No limit switches, so the axes have no end-of-travel protection.
5. Some hardware is unfinished: the winch runs one way only, the telescope encoder is jumpy, and the hook and rope are not built.
6. The load cell and IMU still need calibration on the final assembly, and the load-cell angle correction needs known weights.
7. Boom lean comes from the boom-mounted IMU, so it is a lean warning, not a true chassis tilt.

---

## Future Scope

1. PID-based anti-sway control using IMU feedback on the swing axis.
2. Tipping prediction from the outrigger FSRs, load and lean (LSTM planned).
3. Operator scoring from the logged data.
4. Limit switches and, if wanted, an optional motion lockout on overload.
5. A second IMU on the base for a real chassis tilt reading.
6. Integration on a real crane with a hydraulic pressure transducer as the load sensor, and field validation.
7. Wireless (WiFi) telemetry and a 3D crane visualisation.

---

## Research Paper / Publication

| Item                      | Details                                                   |
| ------------------------- | --------------------------------------------------------- |
| Paper Title               |                                                           |
| Conference / Journal Name |                                                           |
| Paper Status              | Not started (planned for October 2026) |
| Submission Date           |                                                           |
| Paper Link                |                                                           |

---

## References

Add references in IEEE format. The entries below were listed at the start of the project; complete the journal or conference name, volume, pages and year for each.

```text
[1] Junqi Li, Qing Dong, "A Development Method for Load Adaptive Matching Digital Twin System of Bridge Cranes"
[2] Dae-Ho Jang, Gi-Tae Roh, "Simulation-Based Optimization of Crane Lifting Position and Capacity Using a Construction Digital Twin for Prefabricated Bridge Deck Assembly"
[3] Yihai Fang, Yong K. Cho, "A Framework for Real-time Pro-active Safety Assistance for Mobile Crane Lifting Operations"
[4] AS5600 12-bit contactless magnetic rotary position sensor, datasheet.
[5] MPU-6050 six-axis motion tracking device, datasheet.
[6] HX711 24-bit analog-to-digital converter for weigh scales, datasheet.
[7] ESP32 series, datasheet.
```

---

## Repository Update Guidelines

Each student team must update the GitHub repository regularly.

Minimum expected updates:

* Update README every week.
* Push code changes regularly.
* Upload circuit diagrams, CAD files, PCB files, reports and presentations.
* Add weekly progress in the progress table.
* Maintain proper folder structure.
* Do not upload unnecessary temporary files.
* Each major update should have a meaningful commit message.

Example commit messages:

````text
Added problem statement and objectives
Updated system architecture diagram
Added sensor interfacing code
Updated weekly progress for Week 3
Added testing results and prototype images
````

---

## Declaration

We declare that this project work is carried out by our team as part of the BE Capstone Project. The work will be regularly updated on GitHub and all references used will be properly cited.

---

## License

This project is for academic use only.

Optional:

````text
MIT License / Creative Commons / Institute Use Only
````
