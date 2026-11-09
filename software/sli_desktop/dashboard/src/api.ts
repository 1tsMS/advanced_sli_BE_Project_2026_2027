/**
 * Backend addresses, in ONE place. The backend (software/sli_desktop/backend)
 * runs on this PC at port 8000; VITE_BACKEND overrides it (e.g. a test backend:
 * VITE_BACKEND=http://localhost:8001 npm run dev).
 */
const BACKEND: string = import.meta.env.VITE_BACKEND ?? "http://localhost:8000";

export const BACKEND_URL = BACKEND;
export const API_BASE = `${BACKEND}/api`;
export const WS_URL = `${BACKEND.replace(/^http/, "ws")}/ws/telemetry`;

/** POST a raw command line to the ESP32 (validated by the backend). */
export async function sendCommand(command: string): Promise<{ ok: boolean; message?: string }> {
  try {
    const r = await fetch(`${API_BASE}/command`, {
      method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ command }),
    });
    return await r.json();
  } catch {
    return { ok: false, message: "Backend offline" };
  }
}
