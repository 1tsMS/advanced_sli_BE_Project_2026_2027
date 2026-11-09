import { useState } from "react";
import type { SafetyStatus } from "../../types";
import { API_BASE } from "../../api";


const CUT_MODES: { id: "P" | "R" | "S"; label: string; hint: string }[] = [
  { id: "P", label: "Predictive", hint: "Fixed look-ahead step per axis (default at every boot)" },
  { id: "R", label: "Reactive", hint: "Cut only once the limit is reached (baseline for test E5)" },
  { id: "S", label: "Speed-aware", hint: "Look-ahead grows with axis speed x 0.25 s" },
];

/**
 * Experiment switches (tests E1 and E5). Both live on the controllers and are
 * NOT saved: every ESP32 boot is back to predictive cut + idle release on.
 * The active values come back in the $S status, so this card shows what the
 * crane is really doing, not what was last clicked.
 */
export function TestModesCard({ safety }: { safety: SafetyStatus | null }) {
  const [msg, setMsg] = useState("");

  const send = async (command: string) => {
    try {
      const r = await fetch(`${API_BASE}/command`, {
        method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ command }),
      });
      const d = await r.json();
      setMsg(d.ok ? `Sent ${command}` : (d.message ?? "Failed"));
    } catch { setMsg("Backend offline"); }
  };

  const cut = safety?.cutMode;
  const drv = safety?.drvIdle;
  const known = drv !== undefined && drv !== null;

  return (
    <div className="settings-card">
      <div className="section-label">Test modes (not saved: reset on every ESP32 boot)</div>

      <div className="form-row">
        <span className="form-label">SLI cut</span>
        <select className="form-select" value={cut ?? ""} onChange={e => send(`SLI CUT ${e.target.value}`)}
                disabled={!known}>
          {!known && <option value="">firmware too old</option>}
          {CUT_MODES.map(m => <option key={m.id} value={m.id} title={m.hint}>{m.label}</option>)}
        </select>
      </div>
      <div className="dl-muted">{CUT_MODES.find(m => m.id === cut)?.hint ?? ""}</div>

      <div className="form-row">
        <span className="form-label">Stepper drivers</span>
        <select className="form-select" value={known ? (drv ? "1" : "0") : ""} disabled={!known}
                onChange={e => send(`DRV IDLE ${e.target.value}`)}>
          {!known && <option value="">firmware too old</option>}
          <option value="1">Off when idle (0.5 s)</option>
          <option value="0">Always on (holding)</option>
        </select>
      </div>
      <div className="dl-muted">
        Test E1: record the boom angle noise with the drivers always on, then off when idle. {msg}
      </div>
    </div>
  );
}
