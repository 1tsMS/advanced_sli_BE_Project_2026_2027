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
from contextlib import asynccontextmanager

import uvicorn
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from sli_backend.config.settings import (
    API_HOST, API_PORT, CORS_ORIGINS
)
from sli_backend.core.serial_manager import SerialManager
from sli_backend.core.packet_parser import PacketParser
from sli_backend.core.command_router import CommandRouter
from sli_backend.core.session_logger import SessionLogger
from sli_backend.core.load_chart import LoadChartManager
from sli_backend.api.models import TelemetryFrame, DebugReport
from sli_backend.api.ws_endpoint import (
    telemetry_websocket_endpoint,
    broadcast_telemetry,
    broadcast_debug,
    broadcast_ack,
)
from sli_backend.api.rest_endpoints import router as rest_router, init_endpoints

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
chart_mgr    = LoadChartManager(cmd_router)

# ======================== LIFESPAN (STARTUP / SHUTDOWN) ========================

@asynccontextmanager
async def lifespan(_app: FastAPI):
    """Wire up subsystems and start the packet loop; clean up on shutdown."""
    logger.info("=== Advanced SLI Backend starting ===")

    # Inject serial + logging into REST endpoints
    init_endpoints(serial_mgr, cmd_router, session_log, chart_mgr)

    # Register PacketParser callbacks → WebSocket + logger
    pkt_parser.set_callbacks(
        on_telemetry=_on_telemetry,
        on_debug=_on_debug,
        on_ack=_on_ack,
        on_loadchart=chart_mgr.handle_line,
    )

    # Launch background packet processing loop
    loop_task = asyncio.create_task(packet_processing_loop())

    logger.info(f"REST API:  http://{API_HOST}:{API_PORT}/api/")
    logger.info(f"WebSocket: ws://{API_HOST}:{API_PORT}/ws/telemetry")

    yield

    # Graceful cleanup
    loop_task.cancel()
    if session_log.is_active:
        session_log.end_session()
    if serial_mgr.is_connected:
        await serial_mgr.disconnect()
    logger.info("=== Backend shutdown complete ===")


# ======================== FASTAPI APP ========================
app = FastAPI(
    title="Advanced SLI Backend",
    description="Safe Load Indicator middleware — serial ↔ WebSocket bridge",
    version="1.0.0",
    lifespan=lifespan,
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
app.add_api_websocket_route("/ws/telemetry", telemetry_websocket_endpoint)

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
            await asyncio.sleep(0.1)   # never spin (or flood the log) on a persistent error


# ======================== PACKET CALLBACKS ========================
# These run inside the asyncio event loop — can safely call async functions

def _on_telemetry(frame: TelemetryFrame) -> None:
    """Called by PacketParser when a $T frame is decoded."""
    # Fire and forget — schedule broadcast
    asyncio.create_task(broadcast_telemetry(frame))
    # Log to CSV
    session_log.log_frame(frame)


def _on_debug(report: DebugReport) -> None:
    """Called by PacketParser when a complete $D report is collected."""
    asyncio.create_task(broadcast_debug(report))


def _on_ack(ack_line: str) -> None:
    """Called by PacketParser when a $ACK line is received."""
    asyncio.create_task(broadcast_ack(ack_line))


# ======================== ENTRY POINT ========================
if __name__ == "__main__":
    uvicorn.run(
        "main:app",
        host=API_HOST,
        port=API_PORT,
        reload=False,
        log_level="info",
    )
