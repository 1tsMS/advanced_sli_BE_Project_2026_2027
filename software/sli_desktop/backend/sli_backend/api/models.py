"""
Pydantic data models — the contract between Python backend and frontend.
All WebSocket and REST payloads are typed here.
"""
from __future__ import annotations
from typing import Optional, List
from pydantic import BaseModel, Field


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
    ropeLength:    float = 0.0   # mm
    fsr:           List[int] = [0, 0, 0, 0]  # ADC 0-4095 per outrigger
    boomLean:      float = 0.0   # degrees, sideways lean of the boom (IMU on the boom)
    statusFlags:   int   = 0     # bit field, see STATUS_* in firmware config.h
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


class LoadChartEntry(BaseModel):
    """Single load chart row. Limits match firmware load_chart.h."""
    angle:        float = Field(ge=0, le=90)      # boom angle (degrees)
    extensionMM:  float = Field(ge=0, le=2000)    # telescope extension (mm)
    limitKg:      float = Field(ge=0, le=100)     # safe load limit (kg)


class LoadChartUpload(BaseModel):
    """Full load chart to upload to ESP32 (firmware holds at most 32 rows)."""
    entries: List[LoadChartEntry] = Field(min_length=1, max_length=32)


class LoadChartResponse(BaseModel):
    """Load chart as currently stored on the ESP32."""
    entries: List[LoadChartEntry]


class StatusResponse(BaseModel):
    """Backend health and connection state."""
    connected:   bool
    port:        Optional[str] = None
    baudRate:    Optional[int] = None
    rxRate:      float = 0.0   # Actual measured telemetry packets/sec
    backendOK:   bool = True


class ImuCalibrateRequest(BaseModel):
    """IMU zeroing and axis remapping request."""
    boomAxis:    Optional[str] = "Y"   # "X" | "Y"
    tiltAxis:    Optional[str] = "X"   # "X" | "Y"
    boomInvert:  Optional[bool] = False
    tiltInvert:  Optional[bool] = False


class TeleCalibrateRequest(BaseModel):
    """Telescope encoder zeroing and scale/invert configuration request."""
    scale:  Optional[float] = None   # mm per revolution
    invert: Optional[bool]  = None   # direction inversion flag


class LoadGainRequest(BaseModel):
    """Record the load-cell correction at the current boom angle."""
    weightKg: float = Field(ge=0.05, le=100)   # known weight hanging on the hook


class ApiResponse(BaseModel):
    """Generic REST API response envelope."""
    ok:      bool
    message: str = ""
    data:    Optional[dict] = None
