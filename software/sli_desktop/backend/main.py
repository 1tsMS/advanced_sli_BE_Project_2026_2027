"""
Advanced SLI Backend — FastAPI Application Entry Point

Starts two services:
  1. REST API on http://localhost:8000/api/
  2. WebSocket server on ws://localhost:8000/ws/telemetry

Also runs the main packet processing loop that:
  - Reads lines from SerialManager.rx_queue
  - Parses them via PacketParser
  - Broadcasts telemetry/debug to frontend via WebSocket
  - Logs frames to CSV via SessionLogger
"""
from __future__ import annotations
import asyncio
import logging
import sys

import uvicorn
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from sli_backend.config.settings import (
    API_HOST, API_PORT, CORS_ORIGINS, LOG_DIR, PROFILE_DIR
)
from sli_backend.core.serial_manager import SerialManager
from sli_backend.core.packet_parser import PacketParser
from sli_backend.core.command_router import CommandRouter
from sli_backend.core.session_logger import SessionLogger
from sli_backend.core.lift_recorder import LiftRecorder
from sli_backend.core.tipping import TippingStore
from sli_backend.api.models import TelemetryFrame, DebugReport, SafetyStatus
from sli_backend.api.ws_endpoint import (
    telemetry_websocket_endpoint,
    broadcast_telemetry,
    broadcast_debug,
    broadcast_ack,
    broadcast_safety,
    broadcast_chart,
    ws_manager,
)
from sli_backend.api.rest_endpoints import router as rest_router, init_endpoints, set_safety_status
from sli_backend.api.lift_endpoints import router as lift_router, init_lift_endpoints

# ======================== LOGGING ========================
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    handlers=[logging.StreamHandler(sys.stdout)],
)
logger = logging.getLogger(__name__)

# ======================== SUBSYSTEMS ========================
serial_mgr   = SerialManager()
cmd_router   = CommandRouter(serial_mgr)
session_log  = SessionLogger()
pkt_parser   = PacketParser()
lift_rec     = LiftRecorder(LOG_DIR, PROFILE_DIR)
tip_store    = TippingStore(PROFILE_DIR / "tipping.json")
lift_rec.tip_warn_pct = tip_store.cfg.warnPct
cmd_router.on_sent = lift_rec.on_command

# ======================== FASTAPI APP ========================
app = FastAPI(
    title="Advanced SLI Backend",
    description="Safe Load Indicator middleware — serial ↔ WebSocket bridge",
    version="1.0.0",
)

# CORS — allow Electron and Vite dev server
app.add_middleware(
    CORSMiddleware,
    allow_origins=CORS_ORIGINS,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Register routes
app.include_router(rest_router)
app.include_router(lift_router)
app.add_api_websocket_route("/ws/telemetry", telemetry_websocket_endpoint)

# ======================== STARTUP / SHUTDOWN ========================

@app.on_event("startup")
async def startup() -> None:
    """Wire up subsystems and start the packet processing loop."""
    logger.info("=== Advanced SLI Backend starting ===")

    # Inject serial + logging into REST endpoints
    init_endpoints(serial_mgr, cmd_router, session_log)
    init_lift_endpoints(lift_rec, tip_store, cmd_router)

    # USB unplugged / COM port died: close any open recording right away
    serial_mgr.on_disconnect = _on_serial_lost

    # Register PacketParser callbacks → WebSocket + logger
    pkt_parser.set_callbacks(
        on_telemetry=_on_telemetry,
        on_debug=_on_debug,
        on_ack=_on_ack,
        on_safety=_on_safety,
        on_chart=_on_chart,
        on_shaping=_on_shaping,
    )

    # Launch background packet processing loop
    asyncio.create_task(packet_processing_loop())

    logger.info(f"REST API:  http://{API_HOST}:{API_PORT}/api/")
    logger.info(f"WebSocket: ws://{API_HOST}:{API_PORT}/ws/telemetry")


@app.on_event("shutdown")
async def shutdown() -> None:
    """Graceful cleanup."""
    if session_log.is_active:
        session_log.end_session()
    if lift_rec.active:
        lift_rec.stop()
    if serial_mgr.is_connected:
        await serial_mgr.disconnect()
    logger.info("=== Backend shutdown complete ===")


# ======================== PACKET PROCESSING LOOP ========================

async def packet_processing_loop() -> None:
    """
    Continuously drains the serial RX queue and routes parsed packets.
    Runs as a background asyncio task for the lifetime of the server.
    """
    logger.info("Packet processing loop started")
    while True:
        try:
            # Wait for a line (blocks until available)
            line = await asyncio.wait_for(
                serial_mgr.rx_queue.get(), timeout=1.0
            )
            pkt_parser.parse_line(line)

        except asyncio.TimeoutError:
            # No data — just loop again (not an error)
            pass
        except asyncio.CancelledError:
            break
        except Exception as e:
            logger.error(f"Packet loop error: {e}")


# ======================== PACKET CALLBACKS ========================
# These run inside the asyncio event loop — can safely call async functions

def _on_telemetry(frame: TelemetryFrame) -> None:
    """Called by PacketParser when a $T frame is decoded."""
    # Tipping margin from the 4 FSRs (backend-side, warn only)
    tip = tip_store.update(frame.fsr)
    frame.tipValid, frame.tipMargin = tip["valid"], tip["marginPct"]
    frame.copX, frame.copY, frame.tipWarn = tip["copX"], tip["copY"], tip["warn"]
    # Fire and forget — schedule broadcast
    asyncio.create_task(broadcast_telemetry(frame))
    # Log to CSV
    session_log.log_frame(frame)
    lift_rec.on_frame(frame, tip)


def _on_serial_lost(reason: str) -> None:
    """Called by SerialManager when the port drops without a disconnect request."""
    logger.warning(f"ESP32 link lost ({reason})")
    lift_rec.shaping = False           # the ESP32 boots with shaping off
    if session_log.is_active:
        session_log.end_session()


def _on_debug(report: DebugReport) -> None:
    """Called by PacketParser when a complete $D report is collected."""
    asyncio.create_task(broadcast_debug(report))


def _on_ack(ack_line: str) -> None:
    """Called by PacketParser when a $ACK line is received."""
    lift_rec.on_ack(ack_line)
    asyncio.create_task(broadcast_ack(ack_line))


def _on_safety(status: SafetyStatus) -> None:
    """Called by PacketParser for every $S line (SLI state)."""
    set_safety_status(status)          # so GET /api/sli answers before the next $S
    lift_rec.on_safety(status)
    asyncio.create_task(broadcast_safety(status))


def _on_shaping(shp: dict) -> None:
    """Called for every $SHP line (swing input shaping state from the ESP32)."""
    lift_rec.shaping = shp["on"]
    asyncio.create_task(ws_manager.broadcast({"type": "shaping", **shp}))


def _on_chart(chart: dict) -> None:
    """Called by PacketParser when an LC GET readback is complete."""
    asyncio.create_task(broadcast_chart(chart))


# ======================== ENTRY POINT ========================
if __name__ == "__main__":
    uvicorn.run(
        "main:app",
        host=API_HOST,
        port=API_PORT,
        reload=False,
        log_level="info",
    )
