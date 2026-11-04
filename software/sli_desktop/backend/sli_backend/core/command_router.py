"""
CommandRouter — validates and sends G-code commands to ESP32.

Thin validation layer: checks command format before forwarding
to SerialManager.send(). All safety enforcement is on the ESP32.
"""
from __future__ import annotations
import re
import logging
from typing import Callable, Optional

from .serial_manager import SerialManager

logger = logging.getLogger(__name__)

# Valid command patterns
_VALID_PATTERNS = [
    re.compile(r"^T0$"),                                             # Emergency stop
    re.compile(r"^M[1-4]\s+S\d{1,4}\s+D[01]$"),                    # Motor command
    re.compile(r"^M0\s+A[1-4]$"),                                    # Stop axis
    re.compile(r"^CAL0$"),                                           # Tare load cell
    re.compile(r"^CAL1(\s+B[012])?(\s+I[01])?(\s+T[012])?(\s+Q[01])?$"),  # IMU cal (with optional axis/invert params)
    re.compile(r"^CAL2(\s+S[\d.]+)?(\s+I[01])?$"),                 # Reset telescope (with optional scale and invert)
    re.compile(r"^CAL4\s+A[1-4]\s+V-?\d+(\.\d+)?$"),               # Encoder axis: current position = V (A4 = rope)
    re.compile(r"^CAL5\s+A[1-4]\s+I[01]$"),                         # Encoder axis direction (A4 = rope)
    re.compile(r"^CAL6\s+S\d+(\.\d+)?$"),                          # Rope scale (mm per N20 count)
    re.compile(r"^CAL7\s+(W\d+(\.\d+)?|CLEAR|S-?\d+(\.\d+)?)$"),     # Load cell: record weight / clear / set scale
    re.compile(r"^CAL8\s+A[1-3]\s+S\d+(\.\d+)?(\s+I[01])?$"),       # Axis ratio: units per encoder rev
    re.compile(r"^DBG$"),                                            # Debug request
    re.compile(r"^CFG\s+RATE\s+\d+$"),                              # Config rate
    # Load chart upload (firmware load_chart.h): BEGIN, axes, rows, zones, thresholds, COMMIT/GET
    re.compile(r"^LC\s+BEGIN\s+[2-8]\s+[2-6]\s+[\w .-]{1,15}$", re.ASCII),
    re.compile(r"^LC\s+[AE](\s+-?\d+(\.\d+)?){2,8}$"),
    re.compile(r"^LC\s+R\s+[0-7](\s+\d+(\.\d+)?){2,6}$"),
    re.compile(r"^LC\s+Z(\s+\d+(\.\d+)?){3}$"),
    re.compile(r"^LC\s+P(\s+\d+(\.\d+)?){2}$"),
    re.compile(r"^LC\s+(COMMIT|GET)$"),
    re.compile(r"^SLI\s+(ON|OFF|BYPASS\s+\d{1,3})$"),                # Arm / off / timed bypass (<= 600 s on the ESP32)
    re.compile(r"^RST$"),                                            # Clear latched E-stop
    re.compile(r"^SHP\s+[01](\s+L\d{1,4})?$"),                       # Swing input shaping off / on [pendulum mm]
]


class CommandRouter:
    def __init__(self, serial: SerialManager) -> None:
        self._serial = serial
        # Called with every command that reached the serial port (lift recorder:
        # which axis is being driven which way)
        self.on_sent: Optional[Callable[[str], None]] = None

    def _sent(self, command: str) -> None:
        if self.on_sent:
            try:
                self.on_sent(command)
            except Exception as e:
                logger.error(f"on_sent hook failed: {e}")

    def send(self, command: str) -> tuple[bool, str]:
        """
        Validate and forward a G-code command to ESP32.

        Returns:
            (success: bool, message: str)
        """
        command = command.strip()

        if not command:
            return False, "Empty command"

        if not self._serial.is_connected:
            return False, "Not connected to ESP32"

        if not self._is_valid(command):
            logger.warning(f"Rejected invalid command: '{command}'")
            return False, f"Invalid command format: '{command}'"

        success = self._serial.send(command)
        if success:
            self._sent(command)
            # Held motions repeat every 100 ms (dead-man): keep them out of the INFO log
            if command[0] == "M":
                logger.debug(f"CMD → ESP32: {command}")
            else:
                logger.info(f"CMD → ESP32: {command}")
            return True, "OK"
        else:
            return False, "Serial write failed"

    def send_estop(self) -> tuple[bool, str]:
        """Direct E-stop — bypasses validation for speed."""
        if self._serial.is_connected:
            self._serial.send("T0")
            self._sent("T0")
            return True, "E-STOP sent"
        return False, "Not connected"

    def _is_valid(self, command: str) -> bool:
        """Check command against valid pattern list."""
        for pattern in _VALID_PATTERNS:
            if pattern.match(command):
                return True
        return False
