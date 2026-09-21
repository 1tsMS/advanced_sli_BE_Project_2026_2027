# Advanced SLI — Project Checklist

Living to-do list. Last updated: 2026-09-21 (after P4 docs/housekeeping pass).
Legend: `[ ]` open · `[~]` in progress · `[x]` done

Sources: code review of firmware / backend / dashboard (not run or tested) and `project_status_report.md`.

---

## P0 — Safety-critical (fix before any demo)

- [x] **E-stop latch** (2026-09-21, untested on hardware): `g_estopLatched` set by `T0`, blocks M1-M4 until new `RST` command; `sensorTask` keeps `alarmLevel = 3` while latched. Dashboard shows a "Reset E-stop" button when alarm = 3.
- [x] **Mega E-stop disables drivers** (2026-09-21, untested): `emergencyStop()` sets EN high, Mega ignores M1-M3 until `RST`; winch already stopped by ESP32. Switch: `ESTOP_DISABLES_DRIVERS` in `mega_executor.ino`. Check the boom holds with drivers off (worm gear?).
- [x] **Dead-man watchdog** (2026-09-21, untested): Mega and ESP32 winch stop an axis if its M-command isn't repeated within `MOTION_TIMEOUT_MS` (400 ms). New `useHoldToMove` hook resends every 100 ms while held and stops on blur / tab hidden / unmount. NOTE: any other client must now repeat M-commands too. `useGamepad` (unwired) does not yet.
- [ ] **Limit switches** — DEFERRED (2026-09-21): no axis has limit switches yet; implement when they are fitted. Needs: which axes, pins, NO/NC, ESP32 or RAMPS end-stop header. Moved to the future list.
- [x] **Winch speed wrap fixed** (2026-09-21, untested): 0-`MOTOR_SPEED_MAX` (2500) now maps linearly to 0-255 PWM (S1000 = 102). Low speeds may stall the N20; tune on hardware.
- [x] **Overload action** — DECIDED (2026-09-21): alarm only, no motion lockout (also for a faulty load sensor). Nothing to build; SafetyTask never blocks motion.

## P1 — Features that look done but don't work

- [x] **Load chart end to end** (2026-09-21): ESP32 `load_chart.cpp` (LC UPLOAD / entry / SAVE / GET, staged then committed to NVS, max 32 rows), backend `LoadChartManager` waits for `$LC,OK` (502 on error/timeout), `GET /api/loadchart` reads it back. Backend + UI tested against a simulated ESP32; firmware NOT compiled or run on hardware. The stored chart is not used for limits yet (see SafetyTask).
- [x] **Gamepad wired in** (2026-09-21): rewritten on the dead-man `useHoldToMove`; B = E-stop (once per press); speed scales with stick and the speed slider. Tested with a faked controller. Directions come from `src/config/axes.ts`, shared with the on-screen buttons; verify on the real crane (plan says M2/M4 D0 = up, dashboard uses D1 for the up arrow).
- [x] **Load calculation** (2026-09-21): `SafetyTask` computes corrected load, chart limit, load % and alarm level (WARN 80%, CRITICAL 100%, 2% hysteresis). Logic unit-tested on the PC (156 checks); not yet run on hardware.
- [x] **Load cell mounting** — DECIDED (2026-09-21): on top of the boom, cable runs over it, so the reading depends on boom angle and cell orientation (not simply W/cos θ). Solved with a learned correction: hang a known weight and record at 2-4 boom angles (`CAL3 W<kg>`, Debug tab panel). No correction until recorded.
- [x] **`SafetyTask`** (2026-09-21): new task, core 1, priority 6, 50 Hz; pure logic in `safety_eval.cpp`. Chart lookup rule approved: highest limit among rows with angle <= current and extension >= current; 0 kg outside the chart; 5 kg default when no chart is saved; small edge tolerance (0.5 deg / 5 mm).
- [x] **Data Logger tab** (2026-09-21): live 30 s charts (load + limit, boom angle, extension; dependency-free SVG) and saved-session list with CSV download. Tested in browser.
- [x] **Load Chart editor tab** (2026-09-21): editable table, range/duplicate validation, warning when a lower boom or longer reach allows more load, upload + read-back. Tested in browser against a simulated ESP32.

## P2 — Dashboard bugs

- [x] "Sens: X kg" showed raw HX711 counts — fixed 2026-09-21: telemetry `measuredLoad` is now the uncorrected load in kg.
- [x] "Chassis Roll" displayed the boom angle — fixed 2026-09-21: the `$T` roll/pitch fields are replaced by `boomLean` (real sideways lean of the boom) and `statusFlags`; the false "tilt warning" that fired whenever the boom was raised is gone.
- [x] "Tilt" is not a chassis tilt — renamed 2026-09-21 to "Boom Lean" everywhere (panel, gauge, CSV). It moves with swing and boom twist, so it is a lean warning, not a level reading. A true chassis tilt would need a second IMU on the base.
- [x] Boom AS5600 cross-check (2026-09-21): boom encoder is on the pivot shaft 1:1. `boom_check.cpp` compares encoder movement with the IMU angle; the encoder direction is learned automatically the first time the boom moves 10 deg from the Zero-IMU pose (saved to flash, relearned on each re-zero). Disagreement above 3 deg for 0.5 s sets `STATUS_BOOM_MISMATCH` and a critical alarm (alarm only). A dead IMU still sets `STATUS_BOOM_FAULT`. If the boom encoder is missing, the cross-check is simply off.
- [x] `>10 kg` "ERR" logic and alarm colours were duplicated in 3 files — fixed 2026-09-21: the firmware sends `statusFlags` (load / boom / extension sensor fault, no chart, outside chart, lean, E-stop) and `src/status.ts` is the only place the dashboard interprets them.
- [x] Sentinel `loadPercent = 999` — clarified 2026-09-21: 999 now means only "load sensor fault" (alarm 2); real overloads are capped at 500 so they can't be mistaken for it.

## P3 — Hardware (from status report)

- [ ] Replace DRV8833 (N20 runs reverse only); re-verify with `n20_test`.
- [ ] Measure real N20 ticks/rev and drum diameter (840 and 20 mm are guesses); update `config.h`.
- [ ] Design and print hook; build rope + drum assembly.
- [ ] Two-point load cell calibration with known weights on the final setup; make scale runtime-tunable.
- [ ] Static IMU calibration (`CAL1`) on the level, assembled crane.
- [ ] Fix telescope encoder jumpiness: tighten coupling, reduce magnet gap, shorten SoftI2C wires (or change bus/sensor).
- [ ] Equalise outrigger FSRs under a centred load.

## P4 — Docs and housekeeping

- [x] `connections.md` pins fixed 2026-09-21: now matches `config.h` (HX711 GPIO17/16, UART2 GPIO23/27), with a note that `config.h` is the source of truth and a list of where the old test sketches (`conn_test`, `n20_test`) still disagree.
- [x] README/plan mismatch fixed 2026-09-21: README now says "React frontend, Python FastAPI backend" throughout; `software_design_plan.md` got a status banner listing every place the plan differs from what was built.
- [x] README template sections filled 2026-09-21: Hardware/Software Requirements, Technologies, Design Files, Circuit Diagram, Flowchart/Algorithm, Implementation, Code Structure, How to Run, Testing and Results, Applications, Advantages, Limitations, Future Scope, References; Weeks 9-11 of the progress table filled from git history. Weeks 12-15, real result photos and final test outcomes are still open (they depend on hardware work not yet done).
- [x] Filled 2026-09-21 (templates to complete, not final content): `docs/literarture_survey.md` renamed to `literature_survey.md` (typo fixed) with a table to fill in; `hardware/hardware.md`, `reference/paper.md`, `software/software.md` (full rewrite), `images/images.md`.
- [x] Status report rewritten 2026-09-21 (`docs/project_status_report.md`): reflects Phase 3 as code-complete-but-untested-on-hardware, separates what ran on the crane from what is PC/simulator-tested only, and the remaining hardware issues.
- [x] Add `logs/` and `__pycache__/` to `.gitignore` (root `.gitignore` added).
- [x] API now binds to `127.0.0.1` by default (2026-09-21); set `SLI_API_HOST=0.0.0.0` to allow the network. Still no authentication -- fine for a lab bench, not for anything reachable by others.
- [x] Tests added 2026-09-21: 84 backend tests (`software/sli_desktop/backend/tests`, run with `pytest`) covering the packet parser, command whitelist, load chart manager (against a simulated ESP32), CSV logger and REST API; 221 firmware unit tests (`software/firmware/tests`, run with `python run_tests.py`) covering the parser, load chart, gain table, alarm logic and boom cross-check against the real firmware sources.

---

## P5 — Cleanup: unnecessary / leftover code (found 2026-09-21)

Delete outright:
- [x] `dashboard/src/App.css` (not imported anywhere; Vite template leftovers).
- [x] `dashboard/src/assets/hero.png`, `react.svg`, `vite.svg` and `public/icons.svg` (unreferenced).
- [x] `dashboard/README.md` (Vite template boilerplate).
- [x] Unused deps in `package.json`: `framer-motion`, `recharts` (never imported; recharts is heavy). Re-add when the Data Logger chart needs it.
- [x] Unused deps in `requirements.txt`: `python-dotenv`, `websockets` (bundled by `uvicorn[standard]`).
- [x] Unused constants in `backend/config/settings.py`: `WS_HOST`, `WS_PORT` (8765), `TELEMETRY_FIELDS`, `DEFAULT_BAUD_RATE`, `SERIAL_TIMEOUT`; Electron origin `app://.` in CORS.
- [x] Tracked `__pycache__/*.pyc` files in git (`git rm --cached`, add to `.gitignore`).
- [x] Unused firmware members: `motorCommandQueue` (created, never used), `MegaBridge::sendRaw/hasResponse/readResponse`, `N20Encoder::resetCount` and `_lastCount`, `AS5600Driver::_glitchCount` and `REG_RAW_ANGLE`, `FSRReader::readSingle`, `HX711Driver::isReady/setScale/getScale/getOffset/getRawScale`, `MPU6050Driver::getAccel*/getGyro*/setAxisMap/getAxisMap/getRawMPU`, `SensorData::teleAngle`, `BASE_BOOM_LENGTH_MM` (dashboard hardcodes 225).
- [x] `useGamepad.ts` wired in (see P1).

Simplify or fix:
- [x] `MPU6050Driver` axis-remap machinery is bypassed: `sensor_task.cpp` picks source axis, offset and invert itself. Keep one place.
- [x] IMU "Z" axis option in Debug UI and `CAL1` regex is unsupported by firmware (anything except 1 is treated as X).
- [x] `ImuCalibrateRequest` has three names for one thing (`tilt`, `swing`, "chassis"): `swingAxis` and `swingInvert` are aliases.
- [x] Telescope scale: ESP32 flash is now the single source. `DBG` reports `TELE_SCALE`/`TELE_INV` and the Debug tab loads them (no `localStorage`). Fallback default remains in `config.h` and one constant in `DebugTab.tsx`.
- [x] `serial_manager._reader_loop` spins (non-blocking read plus `sleep(0)` through the thread pool). Use a short blocking read or `pyserial-asyncio`.
- [x] `@app.on_event` is deprecated in FastAPI; switch to a lifespan handler.
- [x] `TelemetryPanel` "Safety Interlocks" (cosmetic ARMED/TRIPPED) replaced 2026-09-21 by a "System Status" list where every row is a real firmware flag.
- [ ] `CraneVisualizer` (571 lines) has heavy decoration (hydraulic cylinder, counterweight hazard stripes, CAD grid, ground hatching, glow). The model is a scale robot arm, so a simpler view would do. It also fakes 45° and 500 mm rope when there is no data.
- [x] `Gauge` sentinel hack (`value >= 900` means error) and a no-op glow colour `.replace(...)`; pass an error flag instead.
- [ ] Test sketches overlap: 3 load-cell tests (`hx_711_test`, `loadcell_raw`, `loadcell_approx`), `telescope_tuning` duplicates SoftI2C and calibration now in firmware and dashboard, and `conn_test` maps Lift and Tele to different RAMPS axes than `mega_executor.ino`. Consolidate or archive.
- [ ] `software_design_plan.md` (38 KB) is out of date (Electron, Framer Motion, port 8765, `pyserial-asyncio`, Controller / Calibration tabs, `safety/` folder). Mark as "original plan" or trim, and keep the RTOS tutorial in a separate doc.

Follow-ups from the boom cross-check (2026-09-21):
- [ ] On the crane: run Zero IMU with the boom at its zero pose, raise the boom more than 10 deg and check Debug > Hardware Status shows `Boom IMU vs Encoder` going LEARNING -> OK with a small difference; then move the boom through its range and confirm it stays OK.
- [ ] If it reports MISMATCH while everything works, the encoder is probably not exactly 1:1 (gearing or slip); tell me the observed ratio and I will add a scale factor.
- [ ] Re-zeroing the IMU (CAL1) now also re-references the encoder, so always do it with the boom in its zero pose.

Follow-ups from the P2 work (2026-09-21):
- [ ] The `$T` packet changed (field 11 `boomLean`, field 12 `statusFlags`): flash the new firmware and restart the new backend together. Old firmware packets are rejected with a warning instead of being misread.
- [ ] Old session CSVs keep the old `imuRoll`/`imuPitch` columns; new ones have `boomLean`/`statusFlags`.
- [ ] Check on the crane: unplug the IMU and confirm the dashboard shows POSITION SENSOR FAULT and a critical alarm; unplug the load cell and confirm LOAD SENSOR FAULT within about a second.

Follow-ups from the SafetyTask / load calculation work (2026-09-21):
- [ ] Flash the ESP32 (new files: `load_comp.*`, `safety_eval.*`, `safety_task.*`; `COMMAND_TASK_STACK` now 8192) and check boot shows `Load cell gain: N angle points` and `Load chart: N entries`.
- [ ] Calibrate the angle correction on the crane: tare with nothing hanging, hang a known weight, hold the boom still, record at 3-4 angles (Debug tab > Load Cell Angle Correction). Then check the corrected load stays the same across angles.
- [ ] Upload the real load chart (Load Chart tab) and check the limit and alarms at a few angle/extension positions, including outside the chart (limit 0 kg).
- [ ] Alarm thresholds are now 80% WARN / 100% CRITICAL in firmware (was 85% / 100%) to match the dashboard and design plan; change `ALARM_*_PERCENT` in `config.h` if you prefer otherwise.
- [ ] Optional: move the host-side firmware tests (compiled on the PC with stub Arduino headers) into the repo; they cover the parser, load chart, gain table and alarm logic.

Follow-ups from the P1 work (2026-09-21):
- [ ] Flash the ESP32 and test the load chart: upload from the dashboard, power-cycle, confirm it loads back (`Load chart: N entries in flash` at boot). Also confirm `COMMAND_TASK_STACK` 6144 is enough (no stack overflow resets).
- [ ] With a real controller: check each axis moves the way the stick/button says; if not, flip `negDir/posDir` in `dashboard/src/config/axes.ts`.
- [ ] Backend `GET /api/logs` returns absolute local file paths; drop the `path` field if the dashboard is ever exposed beyond localhost.

Follow-ups from the P0 safety work (2026-09-21):
- [ ] Flash ESP32 + Mega and test: T0 blocks motion and dashboard shows E-STOP; Reset E-stop restores it; hold a motor button then unplug USB / alt-tab (motor stops within ~0.5 s); winch runs at slider speeds.

Follow-ups from the P5 cleanup (2026-09-21):
- [ ] **Compile and flash the ESP32 firmware** (edited but not compiled here: no Arduino toolchain). Check `formatDebugReport` change and MPU6050 driver simplification.
- [ ] On hardware: confirm Debug tab loads telescope scale/invert from the ESP32 on open, and IMU zeroing still works.
- [ ] Dashboard lint has 4 older warnings left (`useTelemetry.ts` connect/reconnect, `SettingsTab.tsx`, two in `DebugTab.tsx`).
- [ ] Still open from P5: `CraneVisualizer` decoration, design plan trim, test sketch consolidation, CAD "offset" files.

Follow-ups from the P4 work (2026-09-21):
- [ ] The README's weekly progress table, results and result images still need filling in as the remaining weeks happen -- that's ongoing project work, not something to script.
- [ ] Decide which CAD "offset"/"2" file variants are current (`hardware/hardware.md` flags this; needs your judgement, not code).
- [ ] `docs/software_design_plan.md` is now historical (status banner at the top); it is not being kept in sync line-by-line as the software design plan.

Note (2026-09-21): a checklist-update script crashed partway through and silently dropped 4 of 7
edits to this section — the underlying doc work was done and verified, but the checkboxes here were
stale until this correction. Recording it so the same silent-failure mode gets watched for going forward.

## Roadmap (not yet started)

- [x] **Phase 3** — SafetyTask, load chart lookup, alarm escalation, load-chart editor UI (code done 2026-09-21; hardware testing pending).
- [ ] **Phase 4** — PID anti-sway on swing axis, sway indicator, audio alarm.
- [ ] Tipping prediction (FSR + load + tilt).
- [ ] Limit switches on the axes (hardware not fitted yet).
- [ ] Operator scoring (LSTM on logged CSVs).
- [ ] 3D visualizer, WiFi telemetry.
- [ ] Real crane retrofit (hydraulic pressure transducer as load proxy), field validation.

## Timeline reminder

Weeks 9–12 (Sept): sensor integration, firmware, calibration + load-chart limits, dashboard.
Weeks 13–15 (Oct): PID + tipping groundwork, full testing, paper.

## Done log

- [x] Phase 1 — end-to-end data pipeline (ESP32 → backend → dashboard)
- [x] Phase 2 — UI, motor controls, debug and calibrate tab, CSV session logger
- [x] Custom drivers: AS5600 (3 buses), SoftI2C, non-blocking HX711
- [x] NVS persistence for telescope scale/invert and IMU calibration
- [x] V1 CAD incl. outriggers and hook model
- [x] P4: docs and housekeeping (connections.md fixed, README filled in, status report rewritten, stub docs written, backend bound to localhost, 84 backend tests + 221 firmware unit tests added)
- [x] Boom IMU vs encoder cross-check (auto-learned direction, mismatch flag + alarm; 221 PC unit checks)
- [x] P2: boom lean + status flags in the packet, centralised fault display, real System Status panel, boom sensor fault alarm (tested against a simulated ESP32 and 187 PC unit checks)
- [x] Phase 3 code: SafetyTask, load chart lookup, angle-correction calibration, alarm hysteresis (unit-tested on PC, not on hardware)
- [x] P1: load chart end to end, gamepad wiring, Data Logger tab, Load Chart editor tab (tested against a simulated ESP32, not on hardware)
- [x] P0: E-stop latch + RST, Mega driver-disable, dead-man watchdog, winch PWM mapping (code only, not yet tested on hardware)
- [x] P5 cleanup steps 1-3 (2026-09-21): unused files/deps/constants removed, `__pycache__` untracked, dead firmware members removed, tilt aliases merged, serial reader no longer spins, lifespan handler, Gauge sentinel removed
