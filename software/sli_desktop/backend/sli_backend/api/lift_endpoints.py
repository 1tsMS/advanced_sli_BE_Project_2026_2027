"""
REST routes for lifts (record / analyse / report), operators, score + cost
settings, tipping geometry and swing input shaping.
"""
from __future__ import annotations

from dataclasses import asdict
from typing import Optional

from fastapi import APIRouter, HTTPException

from ..api.models import LiftStartRequest, OperatorRequest, ShapingRequest, TippingSettings, ApiResponse
from ..core import lift_analysis, tipping
from ..core.lift_recorder import LiftRecorder

router = APIRouter(prefix="/api")

_recorder: Optional[LiftRecorder] = None
_tipping: Optional[tipping.TippingStore] = None
_command_router = None


def init_lift_endpoints(recorder: LiftRecorder, tip_store: tipping.TippingStore, command_router) -> None:
    global _recorder, _tipping, _command_router
    _recorder, _tipping, _command_router = recorder, tip_store, command_router


def _rec() -> LiftRecorder:
    if not _recorder:
        raise HTTPException(500, "Lift recorder not initialized")
    return _recorder


# ---------------- lift ----------------
@router.get("/lift")
async def lift_state():
    return _rec().state()


@router.post("/lift/start")
async def lift_start(req: LiftStartRequest):
    if not req.operator.strip():
        raise HTTPException(400, "Pick an operator first")
    return _rec().start(req.operator, req.note)


@router.post("/lift/stop")
async def lift_stop():
    report = _rec().stop()
    if report is None:
        raise HTTPException(409, "No lift running")
    return report


@router.get("/lifts")
async def lifts():
    return {"lifts": _rec().list_lifts()}


@router.get("/lifts/{lift_id}")
async def lift_report(lift_id: str):
    r = _rec().report(lift_id)
    if r is None:
        raise HTTPException(404, "Lift not found")
    return r


@router.get("/lifts/{lift_id}/series")
async def lift_series(lift_id: str, n: int = 1500):
    rows = _rec().rows(lift_id)
    if rows is None:
        raise HTTPException(404, "Lift not found")
    for r in rows:
        r["load"] = r.get("load") or 0.0
    n = max(50, min(n, 20000))
    return {"rows": lift_analysis.downsample(rows, n), "total": len(rows)}


@router.post("/lifts/{lift_id}/reanalyse")
async def lift_reanalyse(lift_id: str):
    r = _rec().reanalyse(lift_id)
    if r is None:
        raise HTTPException(404, "Lift not found")
    return r


@router.delete("/lifts/{lift_id}", response_model=ApiResponse)
async def lift_delete(lift_id: str):
    if _rec().state().get("id") == lift_id:
        raise HTTPException(409, "Stop the lift first")
    if not _rec().delete(lift_id):
        raise HTTPException(404, "Lift not found")
    return ApiResponse(ok=True, message=f"Deleted {lift_id}")


# ---------------- operators ----------------
@router.get("/operators")
async def operators():
    return {"operators": _rec().operators()}


@router.post("/operators")
async def add_operator(req: OperatorRequest):
    try:
        return {"operators": _rec().add_operator(req.name)}
    except ValueError as e:
        raise HTTPException(400, str(e))


# ---------------- score / cost settings ----------------
@router.get("/lift/settings")
async def lift_settings():
    return asdict(_rec().settings())


@router.put("/lift/settings")
async def save_lift_settings(data: dict):
    try:
        return asdict(_rec().save_settings(data))
    except (TypeError, ValueError) as e:
        raise HTTPException(400, f"Bad settings: {e}")


# ---------------- tipping ----------------
@router.get("/tipping", response_model=TippingSettings)
async def tipping_settings():
    return TippingSettings(**asdict(_tipping.cfg))


@router.put("/tipping", response_model=TippingSettings)
async def save_tipping(req: TippingSettings):
    try:
        _tipping.save(tipping.TippingConfig(**req.model_dump()))
    except ValueError as e:
        raise HTTPException(400, str(e))
    _rec().tip_warn_pct = _tipping.cfg.warnPct
    return TippingSettings(**asdict(_tipping.cfg))


@router.post("/tipping/calibrate", response_model=TippingSettings)
async def calibrate_tipping():
    """Crane at rest, hook empty: even out the 4 FSR sensitivities (CoP -> centre)."""
    gains = tipping.calibration_gains(list(_tipping.recent))
    if gains is None:
        raise HTTPException(409, "Every pad must read a force (crane standing on all 4 FSRs, link up)")
    cfg = tipping.TippingConfig(**{**asdict(_tipping.cfg), "gains": gains, "calibrated": True})
    _tipping.save(cfg)
    return TippingSettings(**asdict(cfg))


# ---------------- swing input shaping ----------------
@router.post("/shaping", response_model=ApiResponse)
async def set_shaping(req: ShapingRequest):
    if not _command_router:
        raise HTTPException(500, "Command router not initialized")
    cmd = "SHP 0" if not req.on else (f"SHP 1 L{int(req.lengthMm)}" if req.lengthMm else "SHP 1")
    ok, msg = _command_router.send(cmd)
    if not ok:
        raise HTTPException(400, msg)
    return ApiResponse(ok=True, message=cmd)
