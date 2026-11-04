import { useCallback, useEffect, useRef, useState } from "react";
import { Play, Square } from "lucide-react";
import type { LiftState } from "../../types";

const API_BASE = "http://localhost:8000/api";
const ADD = "__add__";
const LAST_OP_KEY = "sli.lastOperator";

function mmss(totalSec: number): string {
  const s = Math.max(0, Math.floor(totalSec));
  return `${String(Math.floor(s / 60)).padStart(2, "0")}:${String(s % 60).padStart(2, "0")}`;
}

function readLastOp(): string {
  try { return localStorage.getItem(LAST_OP_KEY) ?? ""; } catch { return ""; }
}

/**
 * Start / Stop Lift with the operator dropdown. The backend records every frame
 * between the two and analyses the lift on stop; onStopped gets the lift id.
 */
export function LiftControl({ onStopped }: { onStopped: (id: string) => void }) {
  const [ops, setOps] = useState<string[]>([]);
  const [op, setOp] = useState(readLastOp);
  const [adding, setAdding] = useState(false);
  const [newName, setNewName] = useState("");
  const [state, setState] = useState<LiftState | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const [now, setNow] = useState(() => Date.now());
  const fetchedAt = useRef(Date.now());

  const refresh = useCallback(async () => {
    try {
      const r = await fetch(`${API_BASE}/lift`);
      if (r.ok) { setState(await r.json()); fetchedAt.current = Date.now(); }
    } catch { setState(null); }
  }, []);

  useEffect(() => {
    fetch(`${API_BASE}/operators`).then(r => r.json()).then(d => {
      setOps(d.operators);
      setOp(o => (d.operators.includes(o) ? o : d.operators[0] ?? ""));
    }).catch(() => setOps([]));
    refresh();
    const id = setInterval(refresh, 2000);
    return () => clearInterval(id);
  }, [refresh]);

  useEffect(() => {
    if (!state?.active) return;
    const id = setInterval(() => setNow(Date.now()), 500);
    return () => clearInterval(id);
  }, [state?.active]);

  useEffect(() => {
    if (!error) return;
    const id = setTimeout(() => setError(""), 4000);
    return () => clearTimeout(id);
  }, [error]);

  const pick = (v: string) => {
    if (v === ADD) { setAdding(true); setNewName(""); return; }
    setOp(v);
    try { localStorage.setItem(LAST_OP_KEY, v); } catch { /* private mode */ }
  };

  const addOperator = async () => {
    const name = newName.trim();
    if (!name) { setAdding(false); return; }
    try {
      const r = await fetch(`${API_BASE}/operators`, {
        method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ name }),
      });
      const d = await r.json();
      if (!r.ok) { setError(typeof d.detail === "string" ? d.detail : "Could not add"); return; }
      setOps(d.operators);
      pick(d.operators.find((o: string) => o.toLowerCase() === name.toLowerCase()) ?? name);
      setAdding(false);
    } catch { setError("Backend offline"); }
  };

  const toggle = async () => {
    setBusy(true);
    try {
      if (state?.active) {
        const r = await fetch(`${API_BASE}/lift/stop`, { method: "POST" });
        const d = await r.json();
        if (r.ok && d.id) onStopped(d.id);
        else setError(typeof d.detail === "string" ? d.detail : "Stop failed");
      } else {
        const r = await fetch(`${API_BASE}/lift/start`, {
          method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ operator: op }),
        });
        if (!r.ok) { const d = await r.json().catch(() => ({})); setError(typeof d.detail === "string" ? d.detail : "Start failed"); }
      }
    } catch { setError("Backend offline"); }
    finally { setBusy(false); refresh(); }
  };

  const active = !!state?.active;
  const elapsed = active ? (state!.elapsedS + (now - fetchedAt.current) / 1000) : 0;

  return (
    <div className="lift-ctl">
      {error && <span className="rec-err" role="alert">{error}</span>}
      {active ? (
        <span className="lift-op-live" title="Operator of the running lift">{state!.operator}</span>
      ) : adding ? (
        <input className="lift-add" autoFocus placeholder="Operator name" value={newName} maxLength={24}
               onChange={e => setNewName(e.target.value)}
               onKeyDown={e => { if (e.key === "Enter") addOperator(); if (e.key === "Escape") setAdding(false); }}
               onBlur={addOperator} />
      ) : (
        <select className="lift-select" value={op} onChange={e => pick(e.target.value)} title="Operator for the next lift"
                disabled={state === null}>
          {ops.map(o => <option key={o} value={o}>{o}</option>)}
          <option value={ADD}>+ Add operator…</option>
        </select>
      )}
      <button className={`lift-btn ${active ? "on" : ""}`} onClick={toggle} disabled={busy || state === null || (!active && !op)}
              title={active ? "Stop the lift and analyse it" : "Start recording a lift for this operator"}>
        {active ? <Square size={10} /> : <Play size={10} />}
        {active ? <>Stop lift <span className="rec-time">{mmss(elapsed)}</span></> : "Start lift"}
      </button>
    </div>
  );
}
