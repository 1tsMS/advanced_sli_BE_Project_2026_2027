# Firmware tests (run on the PC)

These tests build the **real firmware sources** in `../esp32_main` against tiny stand-in Arduino
headers (`stubs/`), so the safety logic can be checked without an ESP32 or the Arduino toolchain.

```bash
python run_tests.py
```

You need a C++17 compiler: `g++` or `clang++` (on Windows, MSYS2), or simply `pip install ziglang`.
`run_tests.py` finds whichever is available (or use `CXX=...` to choose one).

It does two things:

1. **Compile check** of the parser, load chart, gain table, safety evaluation, boom cross-check,
   telemetry formatter, command task, sensor task and Mega bridge. This catches syntax and type
   errors without flashing anything.
2. **Unit tests** (`test_firmware.cpp`) for:
   - G-code parsing, including `LC`, `CAL3`, `RST`, and speed clamping
   - load chart upload rules, flash persistence, corrupt-data handling, and the conservative limit lookup
   - the boom-angle load correction table (interpolation, replacing, ordering, limits, persistence)
   - alarm levels, hysteresis, sensor-fault handling and the status flags
   - the IMU-versus-encoder boom cross-check (direction learning, wrap-around, tolerance, timing)
   - the `$T` packet layout and the `$D` debug report

`stubs/Preferences.h` behaves like flash: data survives across objects, and a test can make writes fail,
so power cycles and flash errors are covered.

## What this does not cover

Nothing here talks to real sensors or motors. The stubs only make the code compile and let the
logic run; they do not emulate the I2C buses, the HX711, the steppers or FreeRTOS scheduling.
Timing, wiring, stack sizes and real calibration still have to be tested on the crane.
