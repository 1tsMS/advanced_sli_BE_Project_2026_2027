"""
REST API endpoints — all HTTP routes for the SLI backend.
"""
from __future__ import annotations
import logging
from pathlib import Path
from typing import Optional

from fastapi import APIRouter, HTTPException
from fastapi.responses import FileResponse

from ..api.models import (
    CommandRequest, ConnectionRequest, LoadChartUpload, LoadChartResponse,
    StatusResponse, ApiResponse, ImuCalibrateRequest, TeleCalibrateRequest,
    LoadGainRequest
)
from ..api.ws_endpoint import ws_manager
from ..core.load_chart import LoadChartError

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api")

# These are set by main.py after all subsystems are initialized
_serial = None
_command_router = None
_session_logger = None
_chart_manager = None


def init_endpoints(serial, command_router, session_logger, chart_manager) -> None:
    """Inject subsystem dependencies (called from main.py)."""
    global _serial, _command_router, _session_logger, _chart_manager
    _serial = serial
    _command_router = command_router
    _session_logger = session_logger
    _chart_manager = chart_manager


# ------------------------------------------------------------------ #
# Connection management
# ------------------------------------------------------------------ #

@router.get("/status", response_model=StatusResponse)
async def get_status():
    """Backend health + serial connection state."""
    return StatusResponse(
        connected=_serial.is_connected if _serial else False,
        port=_serial.port if _serial else None,
        baudRate=_serial.baud_rate if _serial else None,
        rxRate=round(_serial.rx_rate, 1) if _serial else 0.0,
        backendOK=True,
    )


@router.get("/ports")
async def list_ports():
    """List available serial ports on the host machine."""
    if not _serial:
        return {"ports": []}
    return {"ports": _serial.list_ports()}


@router.post("/connect", response_model=ApiResponse)
async def connect(req: ConnectionRequest):
    """Open serial connection to ESP32."""
    if not _serial:
        raise HTTPException(500, "Serial manager not initialized")

    success = await _serial.connect(req.port, req.baudRate)
    if success:
        # Start a new CSV logging session
        if _session_logger:
            path = _session_logger.start_session()
            logger.info(f"Session log: {path}")
        return ApiResponse(ok=True, message=f"Connected to {req.port}")
    else:
        raise HTTPException(400, f"Failed to connect to {req.port}")


@router.post("/disconnect", response_model=ApiResponse)
async def disconnect():
    """Close serial connection."""
    if not _serial:
        return ApiResponse(ok=True, message="Not connected")

    if _session_logger and _session_logger.is_active:
        _session_logger.end_session()

    await _serial.disconnect()
    return ApiResponse(ok=True, message="Disconnected")


# ------------------------------------------------------------------ #
# Command sending
# ------------------------------------------------------------------ #

@router.post("/command", response_model=ApiResponse)
async def send_command(req: CommandRequest):
    """Send a raw G-code command to ESP32."""
    if not _command_router:
        raise HTTPException(500, "Command router not initialized")

    ok, msg = _command_router.send(req.command)
    if ok:
        return ApiResponse(ok=True, message=msg)
    else:
        raise HTTPException(400, msg)


@router.post("/estop", response_model=ApiResponse)
async def emergency_stop():
    """Immediate emergency stop — highest priority."""
    if not _command_router:
        raise HTTPException(500, "Command router not initialized")

    ok, msg = _command_router.send_estop()
    return ApiResponse(ok=ok, message=msg)


# ------------------------------------------------------------------ #
# Calibration
# ------------------------------------------------------------------ #

@router.post("/calibrate/tare", response_model=ApiResponse)
async def calibrate_tare():
    """Send CAL0 (tare load cell) to ESP32."""
    ok, msg = _command_router.send("CAL0")
    return ApiResponse(ok=ok, message=msg)


@router.post("/calibrate/imu", response_model=ApiResponse)
async def calibrate_imu(req: Optional[ImuCalibrateRequest] = None):
    """Send CAL1 (zero IMU) with optional axis remapping to ESP32.

    Firmware CAL1 params:
      B<0|1>  — boom source axis: 0=Roll(X), 1=Pitch(Y)
      I<0|1>  — boom invert flag
      T<0|1>  — tilt source axis: 0=Roll(X), 1=Pitch(Y)  (opposite of boom)
      Q<0|1>  — tilt invert flag
    """
    if req:
        # Map axis string to index: "X"/"Roll" -> 0, "Y"/"Pitch" -> 1
        def axis_idx(s: Optional[str], default: int) -> int:
            return 1 if (s or "").upper() in ("Y", "PITCH") else (0 if (s or "").upper() in ("X", "ROLL") else default)

        b  = axis_idx(req.boomAxis, 0)          # boom source (default Roll=0)
        t  = axis_idx(req.tiltAxis, 1)          # tilt source (default Pitch=1)
        bi = 1 if req.boomInvert else 0
        ti = 1 if req.tiltInvert else 0
        cmd = f"CAL1 B{b} I{bi} T{t} Q{ti}"
    else:
        # Sensible defaults: boom=Roll(0) inverted, tilt=Pitch(1) not inverted
        cmd = "CAL1 B0 I1 T1 Q0"
    ok, msg = _command_router.send(cmd)
    return ApiResponse(ok=ok, message=msg)


@router.post("/calibrate/tele", response_model=ApiResponse)
async def calibrate_telescope(req: Optional[TeleCalibrateRequest] = None):
    """Send CAL2 to ESP32:
    - Plain CAL2: zeros current position to retracted (0mm)
    - CAL2 S<scale> I<invert>: tunes scale (mm/rev) and direction inversion in NVS flash
    """
    if not _command_router:
        raise HTTPException(500, "Command router not initialized")

    if req and (req.scale is not None or req.invert is not None):
        parts = ["CAL2"]
        if req.scale is not None and req.scale > 0:
            parts.append(f"S{req.scale:.4f}")
        if req.invert is not None:
            parts.append(f"I{1 if req.invert else 0}")
        cmd = " ".join(parts)
    else:
        cmd = "CAL2"

    ok, msg = _command_router.send(cmd)
    return ApiResponse(ok=ok, message=msg)


@router.post("/calibrate/loadgain", response_model=ApiResponse)
async def calibrate_load_gain(req: LoadGainRequest):
    """Record the load-cell correction at the current boom angle (CAL3 W<kg>).

    Hang a known weight on the hook, hold the boom still, then call this. The
    ESP32 averages about half a second, stores the correction in flash and
    reports the result as a $ACK,CAL3 line on the WebSocket.
    """
    if not _command_router:
        raise HTTPException(500, "Command router not initialized")
    ok, msg = _command_router.send(f"CAL3 W{req.weightKg:.3f}")
    return ApiResponse(ok=ok, message=msg)


@router.post("/calibrate/loadgain/clear", response_model=ApiResponse)
async def clear_load_gain():
    """Remove every recorded load-cell correction (back to no correction)."""
    if not _command_router:
        raise HTTPException(500, "Command router not initialized")
    ok, msg = _command_router.send("CAL3 CLEAR")
    return ApiResponse(ok=ok, message=msg)


# ------------------------------------------------------------------ #
# Debug
# ------------------------------------------------------------------ #

@router.post("/debug/scan", response_model=ApiResponse)
async def request_debug_scan():
    """Request I2C scan + sensor status report from ESP32."""
    ok, msg = _command_router.send("DBG")
    return ApiResponse(ok=ok, message=msg)


# ------------------------------------------------------------------ #
# Load chart
# ------------------------------------------------------------------ #

@router.post("/loadchart/upload", response_model=ApiResponse)
async def upload_load_chart(chart: LoadChartUpload):
    """Upload a load chart to the ESP32 and save it to flash.

    Succeeds only once the ESP32 confirms it stored every entry.
    """
    if not _chart_manager:
        raise HTTPException(500, "Load chart manager not initialized")

    seen = set()
    for e in chart.entries:
        key = (round(e.angle, 3), round(e.extensionMM, 3))
        if key in seen:
            raise HTTPException(
                400, f"Duplicate entry for angle {e.angle} / extension {e.extensionMM} mm"
            )
        seen.add(key)

    try:
        saved = await _chart_manager.upload(chart.entries)
    except LoadChartError as exc:
        raise HTTPException(502, str(exc))

    return ApiResponse(ok=True, message=f"Load chart saved on ESP32 ({saved} entries)")


@router.get("/loadchart", response_model=LoadChartResponse)
async def read_load_chart():
    """Read the load chart currently stored in the ESP32's flash."""
    if not _chart_manager:
        raise HTTPException(500, "Load chart manager not initialized")

    try:
        entries = await _chart_manager.read()
    except LoadChartError as exc:
        raise HTTPException(502, str(exc))

    return LoadChartResponse(entries=entries)


# ------------------------------------------------------------------ #
# Session logs
# ------------------------------------------------------------------ #

@router.get("/logs")
async def list_logs():
    """List all saved session CSV files."""
    if not _session_logger:
        return {"sessions": []}
    return {"sessions": _session_logger.list_sessions()}


@router.get("/logs/{filename}")
async def download_log(filename: str):
    """Download a specific session log CSV."""
    from ..config.settings import LOG_DIR
    file_path = LOG_DIR / filename
    if not file_path.exists() or not file_path.is_file():
        raise HTTPException(404, f"Log file '{filename}' not found")
    return FileResponse(str(file_path), media_type="text/csv", filename=filename)
