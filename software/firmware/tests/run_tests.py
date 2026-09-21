#!/usr/bin/env python3
"""
Build and run the firmware unit tests on the PC.

    python run_tests.py

1. Compiles the firmware sources (a check that they at least build, without the Arduino toolchain).
2. Builds test_firmware.cpp together with the real safety sources and runs it.

Needs a C++17 compiler. It looks for, in order: $CXX, g++, clang++, c++, then
`python -m ziglang c++` (install with: pip install ziglang).
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
FW = HERE.parent / "esp32_main"
STUBS = HERE / "stubs"
BUILD = HERE / "build"

# Sources that only need the stub headers to compile (compile check only)
COMPILE_CHECK = [
    "gcode_parser", "load_chart", "load_comp", "safety_eval", "safety_task",
    "boom_check", "telemetry_fmt", "command_task", "sensor_task", "mega_bridge",
]
# Sources linked into the unit test executable (pure logic)
TEST_SOURCES = [
    "gcode_parser", "load_chart", "load_comp", "safety_eval", "telemetry_fmt", "boom_check",
]
FLAGS = ["-std=c++17", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-nullability-completeness"]


def find_compiler():
    cxx = os.environ.get("CXX")
    if cxx:
        return cxx.split()
    for name in ("g++", "clang++", "c++"):
        if shutil.which(name):
            return [name]
    try:
        subprocess.run([sys.executable, "-m", "ziglang", "version"], check=True, capture_output=True)
        return [sys.executable, "-m", "ziglang", "c++"]
    except Exception:
        return None


def run(cmd, **kwargs):
    return subprocess.run(cmd, capture_output=True, text=True, **kwargs)


def main():
    cxx = find_compiler()
    if not cxx:
        print("No C++ compiler found. Install g++/clang++ (for example MSYS2 on Windows), "
              "or run: pip install ziglang")
        return 2
    print("Compiler:", " ".join(cxx))
    BUILD.mkdir(exist_ok=True)
    includes = ["-I", str(STUBS), "-I", str(FW)]

    print("\n[1/2] Compile check of the firmware sources")
    failed = []
    for name in COMPILE_CHECK:
        obj = BUILD / f"{name}.o"
        obj.unlink(missing_ok=True)
        result = run(cxx + FLAGS + includes + ["-c", str(FW / f"{name}.cpp"), "-o", str(obj)])
        problems = [l for l in (result.stdout + result.stderr).splitlines()
                    if ("error" in l or "warning:" in l) and "argument unused" not in l]
        if result.returncode != 0 or not obj.exists() or problems:
            failed.append(name)
            print(f"  FAIL  {name}.cpp")
            for line in problems[:8]:
                print("        " + line)
        else:
            print(f"  ok    {name}.cpp")
    if failed:
        return 1

    print("\n[2/2] Unit tests")
    exe = BUILD / ("test_firmware.exe" if os.name == "nt" else "test_firmware")
    exe.unlink(missing_ok=True)
    result = run(cxx + FLAGS + includes
                 + [str(HERE / "test_firmware.cpp")] + [str(FW / f"{n}.cpp") for n in TEST_SOURCES]
                 + ["-o", str(exe)])
    if result.returncode != 0 or not exe.exists():
        print((result.stdout + result.stderr)[-3000:])
        return 1
    result = run([str(exe)])
    print(result.stdout, end="")
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
