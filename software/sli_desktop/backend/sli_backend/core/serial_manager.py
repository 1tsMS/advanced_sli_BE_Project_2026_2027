"""
SerialManager — async serial port handler.

Responsibilities:
  - Open/close the USB serial port (ESP32)
  - Continuously read lines in a background asyncio task
  - Push raw lines to a shared asyncio.Queue for PacketParser
  - Send G-code command strings to ESP32
  - Track connection state and RX rate
"""
from __future__ import annotations
import asyncio
import time
import logging
from typing import Optional

import serial
import serial.tools.list_ports

logger = logging.getLogger(__name__)


class SerialManager:
    def __init__(self) -> None:
        self._port: Optional[str] = None
        self._baud: int = 115200
        self._serial: Optional[serial.Serial] = None
        self._connected: bool = False
        self._read_task: Optional[asyncio.Task] = None

        # Queue of raw ASCII lines from ESP32 — consumed by PacketParser
        self.rx_queue: asyncio.Queue[str] = asyncio.Queue(maxsize=500)

        # Metrics
        self._rx_count: int = 0
        self._rx_rate: float = 0.0
        self._last_rate_check: float = time.time()

    # ------------------------------------------------------------------ #
    # Public API
    # ------------------------------------------------------------------ #

    @property
    def is_connected(self) -> bool:
        return self._connected and self._serial is not None and self._serial.is_open

    @property
    def port(self) -> Optional[str]:
        return self._port

    @property
    def baud_rate(self) -> int:
        return self._baud

    @property
    def rx_rate(self) -> float:
        """Approximate incoming telemetry packets per second."""
        return self._rx_rate

    def list_ports(self) -> list[dict]:
        """Return available serial ports as list of dicts."""
        ports = serial.tools.list_ports.comports()
        return [
            {"port": p.device, "description": p.description or "Unknown"}
            for p in sorted(ports, key=lambda x: x.device)
        ]

    async def connect(self, port: str, baud: int = 115200) -> bool:
        """Open serial connection and start background reader task."""
        if self._connected:
            await self.disconnect()

        try:
            self._serial = serial.Serial(
                port=port,
                baudrate=baud,
                timeout=0.05,   # Non-blocking read with short timeout
            )
            self._port = port
            self._baud = baud
            self._connected = True

            # Start background async reader
            self._read_task = asyncio.create_task(self._reader_loop())
            logger.info(f"Connected to {port} at {baud} baud")
            return True

        except serial.SerialException as e:
            logger.error(f"Failed to open {port}: {e}")
            self._connected = False
            return False

    async def disconnect(self) -> None:
        """Close the serial port and stop the reader task."""
        self._connected = False

        if self._read_task and not self._read_task.done():
            self._read_task.cancel()
            try:
                await self._read_task
            except asyncio.CancelledError:
                pass

        if self._serial and self._serial.is_open:
            self._serial.close()
            self._serial = None

        self._port = None
        logger.info("Serial port disconnected")

    def send(self, command: str) -> bool:
        """
        Send a G-code command string to ESP32.
        Thread-safe (pyserial write is protected internally).
        """
        if not self.is_connected:
            logger.warning(f"Cannot send '{command}' — not connected")
            return False

        try:
            line = command.strip() + "\n"
            self._serial.write(line.encode("ascii"))
            logger.debug(f"TX: {command}")
            return True
        except serial.SerialException as e:
            logger.error(f"Serial write error: {e}")
            self._connected = False
            return False

    # ------------------------------------------------------------------ #
    # Background reader
    # ------------------------------------------------------------------ #

    async def _reader_loop(self) -> None:
        """
        Continuously reads lines from serial in a non-blocking asyncio loop.
        Pushes complete lines into rx_queue for PacketParser to consume.
        """
        line_buffer = b""

        while self._connected:
            try:
                # Blocking read (up to the port timeout) runs in a worker
                # thread, so the event loop stays free and we don't spin.
                chunk = await asyncio.get_running_loop().run_in_executor(
                    None, self._read_chunk
                )

                if chunk:
                    line_buffer += chunk
                    # Split on newlines — may contain multiple complete lines
                    while b"\n" in line_buffer:
                        line, line_buffer = line_buffer.split(b"\n", 1)
                        decoded = line.decode("ascii", errors="replace").strip()
                        if decoded:
                            try:
                                self.rx_queue.put_nowait(decoded)
                                self._rx_count += 1
                            except asyncio.QueueFull:
                                # Drop oldest if queue full (backpressure)
                                try:
                                    self.rx_queue.get_nowait()
                                    self.rx_queue.put_nowait(decoded)
                                except asyncio.QueueEmpty:
                                    pass

                # Update RX rate every second
                now = time.time()
                elapsed = now - self._last_rate_check
                if elapsed >= 1.0:
                    self._rx_rate = self._rx_count / elapsed
                    self._rx_count = 0
                    self._last_rate_check = now

            except asyncio.CancelledError:
                break
            except Exception as e:
                logger.error(f"Reader loop error: {e}")
                self._connected = False
                break

    def _read_chunk(self) -> bytes:
        """
        Blocking read — called via executor to avoid blocking the event loop.
        Waits up to the port timeout for one byte, then drains the rest of the
        buffer. A SerialException (e.g. USB unplugged) propagates so the reader
        loop can mark the link as disconnected.
        """
        if not (self._serial and self._serial.is_open):
            return b""
        first = self._serial.read(1)
        if not first:
            return b""
        waiting = self._serial.in_waiting
        return first + self._serial.read(waiting) if waiting else first
