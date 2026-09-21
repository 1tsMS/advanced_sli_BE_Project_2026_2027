"""
Application-wide configuration and constants.
"""
import os
from pathlib import Path

# REST API + WebSocket (both served by the same uvicorn instance)
# Localhost only by default: the API can move motors and has no authentication.
# Set SLI_API_HOST=0.0.0.0 to allow other machines on the network.
API_HOST = os.environ.get("SLI_API_HOST", "127.0.0.1")
API_PORT = 8000

# Session logging
LOG_DIR = Path(__file__).parent.parent.parent / "logs"
LOG_DIR.mkdir(parents=True, exist_ok=True)

# CORS (allow the Vite dev server)
CORS_ORIGINS = [
    "http://localhost:5173",
    "http://localhost:3000",
    "http://127.0.0.1:5173",
]
