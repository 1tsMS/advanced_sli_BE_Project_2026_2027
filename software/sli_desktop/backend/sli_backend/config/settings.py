"""
Application-wide configuration and constants.
"""
import os
from pathlib import Path

# REST API (WebSocket on the same port: /ws/telemetry)
# Localhost only by default: the API drives the motors and has no login, so it
# must not be reachable from the rest of the Wi-Fi. Set SLI_API_HOST=0.0.0.0
# to open it up deliberately.
API_HOST = os.environ.get("SLI_API_HOST", "127.0.0.1")
API_PORT = 8000

# Lift recordings (CSV + JSON analysis per lift)
LOG_DIR = Path(__file__).parent.parent.parent / "logs"
LOG_DIR.mkdir(parents=True, exist_ok=True)

# Load chart profiles (one JSON file each, made in the Load Chart tab)
PROFILE_DIR = Path(__file__).parent.parent.parent / "profiles"

# CORS (allow Electron frontend)
CORS_ORIGINS = [
    "http://localhost:5173",   # Vite dev server
    "http://localhost:3000",
    "http://127.0.0.1:5173",
    "app://.",                  # Electron
]
