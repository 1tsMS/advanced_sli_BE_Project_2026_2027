"""
CommandRouter — validates and sends G-code commands to ESP32.

Thin validation layer: checks command format before forwarding
to SerialManager.send(). All safety enforcement is on the ESP32.
"""
from __future__ import annotations
import re
import logging
from typing import Optional

from .serial_manager import SerialManager

logger = logging.getLogger(__name__)

# Valid command patterns
_VALID_PATTERNS = [
    re.compile(r"^T0$"),                                             # Emergency stop (latched on ESP32)
    re.compile(r"^RST$"),                                            # Clear E-stop latch
    re.compile(r"^M[1-4]\s+S\d{1,4}\s+D[01]$"),                    # Motor command
    re.compile(r"^M0\s+A[1-4]$"),                                    # Stop axis
    re.compile(r"^CAL0$"),                                           # Tare load cell
    re.compile(r"^CAL1(\s+B[01])?(\s+I[01])?(\s+T[01])?(\s+Q[01])?$"),  # IMU cal (optional X/Y axis + invert params)
    re.compile(r"^CAL2(\s+S[\d.]+)?(\s+I[01])?$"),                 # Reset telescope (with optional scale and invert)
    re.compile(r"^CAL3(\s+W\d+(\.\d+)?|\s+CLEAR)$"),                # Load-cell angle correction (record / clear)
    re.compile(r"^DBG$"),                                            # Debug request
    re.compile(r"^CFG\s+RATE\s+\d+$"),                              # Config rate
    re.compile(r"^LC\s+UPLOAD\s+\d+$"),                             # Load chart upload start
    re.compile(r"^LC\s+[\d.]+,[\d.]+,[\d.]+$"),                     # Load chart entry
    re.compile(r"^LC\s+SAVE$"),                                      # Load chart save
    re.compile(r"^LC\s+GET$"),                                       # Load chart read-back
]


class CommandRouter:
    def __init__(self, serial: SerialManager) -> None:
        self._serial = serial

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
            # Motion commands are repeated ~10x/s while a button is held (dead-man
            # heartbeat), so keep them out of the INFO log.
            level = logging.DEBUG if command.startswith("M") else logging.INFO
            logger.log(level, f"CMD → ESP32: {command}")
            return True, "OK"
        else:
            return False, "Serial write failed"

    def send_estop(self) -> tuple[bool, str]:
        """Direct E-stop — bypasses validation for speed."""
        if self._serial.is_connected:
            self._serial.send("T0")
            return True, "E-STOP sent"
        return False, "Not connected"

    def _is_valid(self, command: str) -> bool:
        """Check command against valid pattern list."""
        for pattern in _VALID_PATTERNS:
            if pattern.match(command):
                return True
        return False
