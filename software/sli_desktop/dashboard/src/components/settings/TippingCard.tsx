import { useEffect, useState } from "react";

const API_BASE = "http://localhost:8000/api";
const LEG_NAMES = ["FSR1 (FL)", "FSR2 (FR)", "FSR3 (RL)", "FSR4 (RR)"];

interface TipCfg { legs: number[][]; gains: number[]; minTotal: number; warnPct: number; calibrated: boolean; }

/**
 * Outrigger geometry for the tipping margin (backend core/tipping.py).
 * Pads must be listed IN ORDER AROUND the base (not criss-cross), in mm from
 * the slew centre: the margin is the centre of pressure's distance to the
 * nearest pad-to-pad edge.
 */
export function TippingCard() {
  const [cfg, setCfg] = useState<TipCfg | null>(null);
  const [msg, setMsg] = useState("");

  useEffect(() => { fetch(`${API_BASE}/tipping`).then(r => r.json()).then(setCfg).catch(() => setCfg(null)); }, []);

  const save = async (next: TipCfg) => {
    try {
      const r = await fetch(`${API_BASE}/tipping`, {
        method: "PUT", headers: { "Content-Type": "application/json" }, body: JSON.stringify(next),
      });
      const d = await r.json();
      if (r.ok) { setCfg(d); setMsg("Saved"); } else setMsg(typeof d.detail === "string" ? d.detail : "Save failed");
    } catch { setMsg("Backend offline"); }
  };

  const calibrate = async () => {
    try {
      const r = await fetch(`${API_BASE}/tipping/calibrate`, { method: "POST" });
      const d = await r.json();
      if (r.ok) { setCfg(d); setMsg(`Calibrated: gains ${d.gains.map((g: number) => g.toFixed(2)).join(", ")}`); }
      else setMsg(typeof d.detail === "string" ? d.detail : "Calibration failed");
    } catch { setMsg("Backend offline"); }
  };

  if (!cfg) return <div className="settings-card"><div className="section-label">Tipping margin</div><div className="dl-muted">Backend offline.</div></div>;

  const setLeg = (i: number, j: number, v: number) => {
    const legs = cfg.legs.map(l => [...l]);
    legs[i][j] = v;
    setCfg({ ...cfg, legs });
  };

  return (
    <div className="settings-card">
      <div className="section-label">Tipping margin (FSR outriggers)</div>
      <div className="dl-muted">
        Pad centres in mm from the slew centre, in order around the base. Margin = centre of pressure's
        distance to the nearest edge (100 % centred, 0 % = a pad is about to lift). Warning only, no motion cut.
      </div>
      <table className="dl-table">
        <thead><tr><th>Pad</th><th>x (mm)</th><th>y (mm)</th><th>gain</th></tr></thead>
        <tbody>
          {cfg.legs.map((l, i) => (
            <tr key={i}>
              <td>{LEG_NAMES[i]}</td>
              <td><input className="form-input tip-in" type="number" value={l[0]} onChange={e => setLeg(i, 0, Number(e.target.value))} /></td>
              <td><input className="form-input tip-in" type="number" value={l[1]} onChange={e => setLeg(i, 1, Number(e.target.value))} /></td>
              <td>{cfg.gains[i].toFixed(2)}</td>
            </tr>
          ))}
        </tbody>
      </table>
      <div className="form-row">
        <span className="form-label">Warn below</span>
        <input className="form-input" type="number" value={cfg.warnPct} onChange={e => setCfg({ ...cfg, warnPct: Number(e.target.value) })} />
        <span className="dl-muted">%</span>
      </div>
      <div className="form-btn-row">
        <button className="btn-primary" onClick={() => save(cfg)}>Save geometry</button>
        <button className="btn-ghost" onClick={calibrate}
                title="Crane standing on all 4 pads, hook empty, boom centred: evens out the 4 sensor sensitivities">
          Calibrate (crane at rest, no load)
        </button>
      </div>
      <div className="dl-muted">{cfg.calibrated ? "Pads calibrated." : "Pads not calibrated yet (gains = 1)."} {msg}</div>
    </div>
  );
}
