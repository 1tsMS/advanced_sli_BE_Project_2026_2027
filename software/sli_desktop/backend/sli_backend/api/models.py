"""
Pydantic data models — the contract between Python backend and frontend.
All WebSocket and REST payloads are typed here.
"""
from __future__ import annotations
from typing import Optional, List
from pydantic import BaseModel


class TelemetryFrame(BaseModel):
    """
    Parsed $T packet from ESP32.
    Field names match the JSON keys sent over WebSocket to the frontend.
    """
    boomAngle:     float = 0.0   # degrees
    extensionMM:   float = 0.0   # mm
    measuredLoad:  float = 0.0   # kg (raw from load cell)
    actualLoad:    float = 0.0   # kg (angle-compensated, Phase 3)
    swingAngle:    float = 0.0   # degrees
    ropeLength:    float = 0.0   # mm, calibrated: load-cell end to the knot
    ropeTotal:     float = 0.0   # mm, ropeLength + fixed path + knot-to-hook (0 = old firmware)
    hookDrop:      float = 0.0   # mm, load-cell end to hook tip (0 = old firmware)
    fsr:           List[int] = [0, 0, 0, 0]  # ADC 0-4095 per outrigger
    imuRoll:       float = 0.0   # degrees
    imuPitch:      float = 0.0   # degrees
    safeLoadLimit: float = 5.0   # kg
    loadPercent:   float = 0.0   # 0-100+
    alarmLevel:    int   = 0     # 0=OK, 1=WARN, 2=CRITICAL, 3=ESTOP
    timestamp:     float = 0.0   # Unix timestamp (added by backend)


class DebugEntry(BaseModel):
    """Single $D debug line from ESP32."""
    bus:     str          # e.g. "I2C0", "HX711", "FSR1"
    address: Optional[str] = None  # e.g. "0x36"
    status:  str          # "OK" or "FAIL" or raw ADC value


class DebugReport(BaseModel):
    """Full debug report (all $D lines collected after DBG command)."""
    entries: List[DebugEntry] = []


class CommandRequest(BaseModel):
    """Raw G-code command to send to ESP32."""
    command: str          # e.g. "M1 S200 D1", "T0", "CAL0"


class ConnectionRequest(BaseModel):
    """Port connection request from frontend."""
    port:     str         # e.g. "COM4" or "/dev/ttyUSB0"
    baudRate: int = 115200


class LoadZone(BaseModel):
    """Swing zone: capacity x factor while the swing angle is in [fromDeg, toDeg) (wraps 360)."""
    fromDeg: float
    toDeg:   float
    factor:  float


class LoadChartProfile(BaseModel):
    """
    One load chart profile, stored as JSON on the PC and uploaded to the ESP32.
    Rows of capacityKg follow anglesDeg, columns follow extensionsMM.
    Limits mirror the firmware (load_chart.h / lc_validate): see core/load_chart.py.
    """
    name:         str
    description:  str = ""
    anglesDeg:    List[float]
    extensionsMM: List[float]
    capacityKg:   List[List[float]]
    zones:        List[LoadZone] = []
    warnPct:      float = 90.0
    cutPct:       float = 100.0
    updated:      Optional[str] = None     # ISO time, set by the backend on save


class SafetyStatus(BaseModel):
    """Parsed $S line: the ESP32's SLI state (5 Hz + on every change)."""
    state:       str = "FAULT"   # OK | WARN | CUT | FAULT | ESTOP
    pct:         float = 0.0
    capKg:       float = 0.0
    zone:        int = -1
    bypassLeftS: int = 0
    blocked:     int = 0         # bit (axis-1)*2 + dir set = that motion is cut
    chart:       str = "NONE"
    mode:        str = "ARMED"   # ARMED | BYPASS | OFF (state/blocked are enforced only when ARMED)
    timestamp:   float = 0.0


class BypassRequest(BaseModel):
    seconds: int


class RecordingState(BaseModel):
    """CSV recording state (recording is off until the dashboard starts it)."""
    recording:  bool = False
    file:       Optional[str] = None   # file name while recording
    rows:       int = 0
    elapsedSec: float = 0.0


class StatusResponse(BaseModel):
    """Backend health and connection state."""
    connected:   bool
    port:        Optional[str] = None
    baudRate:    Optional[int] = None
    rxRate:      float = 0.0   # Actual measured telemetry packets/sec
    backendOK:   bool = True
    recording:   Optional[RecordingState] = None


class EncoderCalibrateRequest(BaseModel):
    """Encoder axis calibration: CAL4 (set current value) and/or CAL5 (direction)."""
    axis:   str                       # "swing" | "boom" | "tele"
    value:  Optional[float] = None    # current position is this (deg, or mm for tele)
    invert: Optional[bool]  = None    # direction


class RopeCalibrateRequest(BaseModel):
    """Rope (N20 winch encoder) scale: mm of rope per quadrature count."""
    scale:  float
    invert: Optional[bool] = None


class LoadCalibrateRequest(BaseModel):
    """Load cell calibration: record a known weight, clear the points, or set the scale."""
    action: str                      # "point" | "clear" | "scale"
    weight: Optional[float] = None   # kg, for "point"
    scale:  Optional[float] = None   # counts per kg, for "scale"


class AxisScaleRequest(BaseModel):
    """Ratio of an encoder axis: units (deg or mm) per encoder revolution."""
    axis:   str                      # "swing" | "boom" | "tele"
    scale:  float
    invert: Optional[bool] = None


class ImuCalibrateRequest(BaseModel):
    """IMU zeroing and axis remapping request."""
    boomAxis:    Optional[str] = "Y"
    tiltAxis:    Optional[str] = "X"
    swingAxis:   Optional[str] = "X"
    boomInvert:  Optional[bool] = False
    tiltInvert:  Optional[bool] = False
    swingInvert: Optional[bool] = False


class TeleCalibrateRequest(BaseModel):
    """Telescope encoder zeroing and scale/invert configuration request."""
    scale:  Optional[float] = None   # mm per revolution
    invert: Optional[bool]  = None   # direction inversion flag


class ApiResponse(BaseModel):
    """Generic REST API response envelope."""
    ok:      bool
    message: str = ""
    data:    Optional[dict] = None
