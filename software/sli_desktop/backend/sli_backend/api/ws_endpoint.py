"""
WebSocket endpoint — broadcasts telemetry to all connected frontend clients.

All connected clients receive every TelemetryFrame in real-time.
Also forwards $ACK and $D debug reports as distinct message types.
"""
from __future__ import annotations
import asyncio
import json
import logging
from typing import Set

from fastapi import WebSocket, WebSocketDisconnect

from ..api.models import TelemetryFrame, DebugReport, SafetyStatus

logger = logging.getLogger(__name__)


class ConnectionManager:
    """Tracks all active WebSocket connections."""

    def __init__(self) -> None:
        self._clients: Set[WebSocket] = set()

    async def connect(self, ws: WebSocket) -> None:
        await ws.accept()
        self._clients.add(ws)
        logger.info(f"WS client connected. Total: {len(self._clients)}")

    def disconnect(self, ws: WebSocket) -> None:
        self._clients.discard(ws)
        logger.info(f"WS client disconnected. Total: {len(self._clients)}")

    async def broadcast(self, message: dict) -> None:
        """Send a dict as JSON to all connected clients."""
        if not self._clients:
            return

        payload = json.dumps(message)
        dead: Set[WebSocket] = set()

        for ws in list(self._clients):
            try:
                await ws.send_text(payload)
            except Exception:
                dead.add(ws)

        # Clean up dead connections
        for ws in dead:
            self._clients.discard(ws)

    @property
    def client_count(self) -> int:
        return len(self._clients)


# Singleton — shared across the app
ws_manager = ConnectionManager()


async def telemetry_websocket_endpoint(ws: WebSocket) -> None:
    """
    FastAPI WebSocket route handler.
    Accepts a connection and keeps it alive. Data is pushed
    externally via ws_manager.broadcast() from the packet loop.
    """
    await ws_manager.connect(ws)
    try:
        while True:
            # Keep connection alive — wait for any client message
            # (frontend can send pings or config updates)
            data = await ws.receive_text()
            # Currently we just echo pings back — extend later if needed
            if data == "ping":
                await ws.send_text("pong")
    except WebSocketDisconnect:
        pass
    finally:
        ws_manager.disconnect(ws)


async def broadcast_telemetry(frame: TelemetryFrame) -> None:
    """Push a telemetry frame to all connected WS clients."""
    msg = {"type": "telemetry", **frame.model_dump()}
    await ws_manager.broadcast(msg)


async def broadcast_debug(report: DebugReport) -> None:
    """Push a debug report to all connected WS clients."""
    msg = {
        "type": "debug",
        "entries": [e.model_dump() for e in report.entries],
    }
    await ws_manager.broadcast(msg)


async def broadcast_ack(ack_line: str) -> None:
    """Push an acknowledgement string to all connected WS clients."""
    msg = {"type": "ack", "data": ack_line}
    await ws_manager.broadcast(msg)


async def broadcast_safety(status: SafetyStatus) -> None:
    """Push the ESP32's SLI status ($S) to all connected WS clients."""
    await ws_manager.broadcast({"type": "safety", **status.model_dump()})


async def broadcast_chart(chart: dict) -> None:
    """Push the chart read back from the ESP32 (LC GET) to all connected WS clients."""
    await ws_manager.broadcast({"type": "chart", "chart": None if chart.get("none") else chart})
