import { useEffect, useMemo, useState } from "react";
import { Printer, RefreshCcw, SlidersHorizontal } from "lucide-react";
import type { LiftReport, LiftSummary } from "../../types";
import { fetchLifts, fetchReport, liftLabel } from "../datalogger/DataLoggerTab";
import { PhaseBar } from "../datalogger/LiftReview";
import { fmtS } from "../datalogger/chartKit";
import "../datalogger/datalogger.css";
import { API_BASE } from "../../api";

const PEN_LABEL: Record<string, string> = {
  cutEvent: "Each entry into CUT", warnEvent: "Each entry into WARN", cutPerS: "Per second in CUT",
  warnPerS: "Per second in WARN", snatch: "Each snatch pick-up", tipWarnPerS: "Per second near tipping",
  refusal: "Each motion refused by SLI", estop: "Each E-stop", sliNotArmed: "Lift with SLI not armed",
};

type Section = "safety" | "timeline" | "lifts";
const SECTIONS: { id: Section; label: string }[] = [
  { id: "safety", label: "Safety" },
  { id: "timeline", label: "Timeline" },
  { id: "lifts", label: "All lifts" },
];

function ScoreBar({ label, value, max, note }: { label: string; value: number; max: number; note: string }) {
  return (
    <div>
      <div className="op-bar-top"><span>{label}</span><b>{value.toFixed(1)} <small>/ {max}</small></b></div>
      <div className="op-bar-track"><span style={{ width: `${(value / max) * 100}%` }} /></div>
      <div className="op-bar-note">{note}</div>
    </div>
  );
}

function Metric({ label, value, level = "", hint }: { label: string; value: string; level?: "" | "warn" | "crit"; hint?: string }) {
  return (
    <div className={`dl-stat ${level}`} title={hint}>
      <span className="dl-stat-label">{label}</span>
      <span className="dl-stat-val">{value}</span>
    </div>
  );
}

function SettingsEditor({ onSaved }: { onSaved: () => void }) {
  const [s, setS] = useState<Record<string, unknown> | null>(null);
  const [msg, setMsg] = useState("");
  useEffect(() => { fetch(`${API_BASE}/lift/settings`).then(r => r.json()).then(setS).catch(() => setS(null)); }, []);
  if (!s) return <div className="dl-muted">Backend offline.</div>;
  const pen = s.pen as Record<string, number>;
  const save = async () => {
    const r = await fetch(`${API_BASE}/lift/settings`, {
      method: "PUT", headers: { "Content-Type": "application/json" }, body: JSON.stringify(s),
    }).catch(() => null);
    setMsg(r?.ok ? "Saved. Re-score lifts to apply." : "Save failed");
    if (r?.ok) onSaved();
  };
  const field = (label: string, value: number, step: number, set: (v: number) => void, key: string) => (
    <label className="op-field" key={key}>
      <span>{label}</span>
      <input className="form-input" type="number" step={step} value={value} onChange={e => set(Number(e.target.value))} />
    </label>
  );
  return (
    <div className="op-settings">
      <div className="op-settings-col">
        <div className="dl-label">Scoring</div>
        {field("Penalty points for a safety score of 0", s.safetyZeroAt as number, 1, v => setS({ ...s, safetyZeroAt: v }), "z")}
        {field("Snatch above DAF", s.snatchDaf as number, 0.05, v => setS({ ...s, snatchDaf: v }), "d")}
        {field("Boom-tip shock above (m/s²)", s.shockAccel as number, 0.1, v => setS({ ...s, shockAccel: v }), "a")}
      </div>
      <div className="op-settings-col">
        <div className="dl-label">Penalty points</div>
        {Object.keys(pen).map(k => field(PEN_LABEL[k] ?? k, pen[k], 0.5, v => setS({ ...s, pen: { ...pen, [k]: v } }), k))}
      </div>
      <div className="op-settings-actions">
        <button className="btn-primary" onClick={save}>Save settings</button>
        {msg && <span className="dl-muted">{msg}</span>}
      </div>
    </div>
  );
}

/**
 * Operator report, in three levels: the verdict (grade, three sub-scores, four
 * key numbers), then ONE section at a time (safety / timeline / all lifts),
 * so the screen never turns into a wall of text.
 */
export function OperatorTab({ selectedLift, onSelectLift, liftsVersion }: {
  selectedLift: string | null; onSelectLift: (id: string | null) => void; liftsVersion: number;
}) {
  const [lifts, setLifts] = useState<LiftSummary[]>([]);
  const [report, setReport] = useState<LiftReport | null>(null);
  const [who, setWho] = useState("");
  const [showSettings, setShowSettings] = useState(false);
  const [section, setSection] = useState<Section>("safety");
  const [busy, setBusy] = useState(false);
  const [bump, setBump] = useState(0);

  useEffect(() => { fetchLifts().then(setLifts); }, [liftsVersion, bump]);
  const current = selectedLift ?? lifts[0]?.id ?? null;
  useEffect(() => {
    setReport(null);
    if (current) fetchReport(current).then(setReport);
  }, [current, liftsVersion, bump]);

  const operators = useMemo(() => Array.from(new Set(lifts.map(l => l.operator ?? "?"))), [lifts]);
  const shown = lifts.filter(l => l.ok && (!who || l.operator === who));
  const perOp = useMemo(() => operators.map(o => {
    const ls = lifts.filter(l => l.ok && l.operator === o && l.score !== null);
    const avg = (f: (l: LiftSummary) => number) => ls.length ? ls.reduce((a, l) => a + f(l), 0) / ls.length : 0;
    return { op: o, n: ls.length, avgScore: avg(l => l.score ?? 0), best: Math.max(0, ...ls.map(l => l.score ?? 0)),
             avgDur: avg(l => l.durationS ?? 0) };
  }).filter(r => r.n), [lifts, operators]);

  const rescore = async (all: boolean) => {
    setBusy(true);
    const ids = all ? lifts.map(l => l.id) : current ? [current] : [];
    for (const id of ids) await fetch(`${API_BASE}/lifts/${id}/reanalyse`, { method: "POST" }).catch(() => null);
    setBusy(false);
    setBump(b => b + 1);
  };

  const r = report?.ok ? report : null;
  const overRated = r && r.load.ratedKg ? r.load.liftedKg > r.load.ratedKg : false;

  return (
    <div className="dl-page op-page">
      <div className="dl-topbar no-print">
        <label className="dl-filter">
          Lift
          <select className="dl-select wide" value={current ?? ""} onChange={e => onSelectLift(e.target.value || null)}>
            {lifts.length === 0 && <option value="">No lifts recorded</option>}
            {lifts.map(l => (
              <option key={l.id} value={l.id}>
                {l.operator} · {liftLabel(l)}{l.grade ? ` · ${l.grade} ${l.score?.toFixed(0)}` : ""}
              </option>
            ))}
          </select>
        </label>
        <span className="dl-card-fill" />
        <button className={`dl-icon-btn ${showSettings ? "on" : ""}`} onClick={() => setShowSettings(v => !v)}>
          <SlidersHorizontal size={13} /> Scoring
        </button>
        <button className="dl-icon-btn" disabled={busy || !current} onClick={() => rescore(false)} title="Re-run the analysis with the current settings">
          <RefreshCcw size={13} /> Re-score
        </button>
        <button className="dl-icon-btn" disabled={busy || !lifts.length} onClick={() => rescore(true)}>Re-score all</button>
        <button className="dl-icon-btn" disabled={!r} onClick={() => window.print()} title="Print or save as PDF">
          <Printer size={13} /> Print / PDF
        </button>
      </div>

      {showSettings && <div className="dl-card no-print"><SettingsEditor onSaved={() => setBump(b => b + 1)} /></div>}

      {!current && <div className="dl-empty">No lifts yet. Pick an operator in the header, press Start lift, do the lift, press Stop lift.</div>}
      {current && !report && <div className="dl-empty">Loading report…</div>}
      {report && !report.ok && <div className="dl-empty">{report.reason}</div>}

      {r && (
        <div className="op-report">
          {/* ---- level 1: the verdict ---- */}
          <div className="dl-card op-hero">
            <div className="op-verdict">
              <div className={`op-grade g${r.score.grade}`}>
                <span className="op-grade-letter">{r.score.grade}</span>
                <span className="op-grade-num">{r.score.total.toFixed(0)} / 100</span>
              </div>
              <div className="op-who">
                <span className="op-who-name">{r.operator}</span>
                <span className="op-who-meta">{new Date(r.startedAt).toLocaleString()}</span>
                <span className="op-who-meta">{fmtS(r.durationS)} · {r.id}</span>
                {r.note && <span className="op-who-meta">“{r.note}”</span>}
              </div>
            </div>

            <div className="op-bars">
              <ScoreBar label="Safety" value={r.score.safety} max={r.score.max.safety}
                        note={`${r.penalty.points.toFixed(1)} penalty points`} />
              <ScoreBar label="Smoothness" value={r.score.smoothness} max={r.score.max.smoothness}
                        note={`peak DAF ${r.smoothness.dafMax.toFixed(2)} · ripple ${r.smoothness.meanRippleG.toFixed(1)} g · ${r.smoothness.reversalsPerMin} reversals/min`} />
              <ScoreBar label="Efficiency" value={r.score.efficiency} max={r.score.max.efficiency}
                        note={`${(r.efficiency.idleFrac * 100).toFixed(0)} % idle · ${r.smoothness.jogsPerMin} motion starts/min`} />
            </div>

            <div className="op-keys">
              <Metric label="Lifted" value={`${(r.load.liftedKg * 1000).toFixed(0)} g`} level={overRated ? "crit" : ""}
                      hint={r.load.ratedKg ? `Rated ${(r.load.ratedKg * 1000).toFixed(0)} g at that pose` : undefined} />
              <Metric label="Rated" value={r.load.ratedKg ? `${(r.load.ratedKg * 1000).toFixed(0)} g` : "—"} />
              <Metric label="Peak load %" value={r.load.peakPct !== null ? `${r.load.peakPct} %` : "—"}
                      level={r.load.peakPct !== null && r.load.peakPct >= 100 ? "crit" : r.load.peakPct !== null && r.load.peakPct >= 90 ? "warn" : ""} />
              <Metric label="SLI mode" value={r.safety.modes.join(", ")} level={r.safety.modes.some(m => m !== "ARMED") ? "warn" : ""} />
            </div>
          </div>

          {/* ---- level 2: one section at a time ---- */}
          <div className="dl-topbar no-print">
            <div className="dl-seg" role="tablist" aria-label="Report section">
              {SECTIONS.map(s => (
                <button key={s.id} role="tab" aria-selected={section === s.id}
                        className={section === s.id ? "on" : ""} onClick={() => setSection(s.id)}>{s.label}</button>
              ))}
            </div>
          </div>

          {section === "safety" && (
            <div className="op-section two">
              <div className="dl-card">
                <div className="dl-card-head"><span className="dl-card-title">Where the safety points went</span></div>
                <table className="dl-table">
                  <thead><tr><th>Event</th><th className="num">Count / s</th><th className="num">Points</th></tr></thead>
                  <tbody>
                    {r.penalty.items.length === 0 && <tr><td colSpan={3}>None. Clean lift.</td></tr>}
                    {r.penalty.items.map(i => (
                      <tr key={i.what}><td>{i.what}</td><td className="num">{i.value}</td><td className="num">{i.points.toFixed(1)}</td></tr>
                    ))}
                    {r.penalty.items.length > 0 && (
                      <tr className="total"><td>Total</td><td /><td className="num">{r.penalty.points.toFixed(1)}</td></tr>
                    )}
                  </tbody>
                </table>
              </div>
              <div className="dl-card">
                <div className="dl-card-head"><span className="dl-card-title">Safety numbers</span></div>
                <div className="dl-now-grid">
                  <Metric label="Time in WARN" value={`${r.safety.warnS} s`} level={r.safety.warnS > 0 ? "warn" : ""} />
                  <Metric label="Time in CUT" value={`${r.safety.cutS} s`} level={r.safety.cutS > 0 ? "crit" : ""} />
                  <Metric label="Refused motions" value={String(r.safety.refusals)} />
                  <Metric label="Mean load %" value={r.load.meanPct !== null ? `${r.load.meanPct} %` : "—"} />
                  <Metric label="Min tipping margin" value={r.safety.minTipMargin !== null ? `${r.safety.minTipMargin} %` : "—"} />
                  <Metric label="E-stops" value={String(r.safety.estops)} />
                  <Metric label="Peak DAF" value={r.smoothness.dafMax.toFixed(2)} hint="Dynamic amplification at pick-up: peak / static load" />
                  <Metric label="Swing-stop ripple" value={r.smoothness.swingStopRippleG !== null ? `${r.smoothness.swingStopRippleG} g` : "—"}
                          hint="Load-cell ripple in the 2 s after the swing stops (sway proxy)" />
                  <Metric label="Boom-tip shocks" value={r.dynamics ? String(r.dynamics.shocks.length) : "—"}
                          hint={r.dynamics?.peakAccel != null ? `Peak ${r.dynamics.peakAccel} m/s²` : "Recorded before acceleration logging"} />
                </div>
              </div>
            </div>
          )}

          {section === "timeline" && (
            <div className="op-section">
              <div className="dl-card">
                <div className="dl-card-head"><span className="dl-card-title">Lift phases</span></div>
                <PhaseBar phases={r.phases} duration={r.durationS || 1} />
                <div className="op-phase-totals">
                  {Object.entries(r.phaseTotals).map(([k, v]) => (
                    <Metric key={k} label={k} value={`${fmtS(v)} · ${((v / (r.durationS || 1)) * 100).toFixed(0)} %`} />
                  ))}
                </div>
              </div>
              <div className="dl-card">
                <div className="dl-card-head"><span className="dl-card-title">Flagged moments ({r.flags.length})</span></div>
                {r.flags.length === 0 ? <div className="dl-muted">Nothing flagged.</div> : (
                  <ul className="op-flags">
                    {r.flags.map((f, i) => (
                      <li key={i}>
                        <span className={`state-chip ${f.level === "crit" ? "crit" : f.level === "warn" ? "warn" : "off"}`}>
                          {f.level === "crit" ? "▲ CRIT" : f.level === "warn" ? "△ WARN" : "INFO"}
                        </span>
                        <span className="op-flag-t">{fmtS(f.t)}</span>
                        <span>{f.text}</span>
                      </li>
                    ))}
                  </ul>
                )}
              </div>
            </div>
          )}

          {section === "lifts" && (
            <div className="op-section two">
              <div className="dl-card">
                <div className="dl-card-head">
                  <span className="dl-card-title">All lifts</span>
                  <select className="dl-select" value={who} onChange={e => setWho(e.target.value)}>
                    <option value="">All operators</option>
                    {operators.map(o => <option key={o} value={o}>{o}</option>)}
                  </select>
                </div>
                <table className="dl-table clickable">
                  <thead><tr><th>When</th><th>Operator</th><th className="num">Load</th><th className="num">Time</th><th className="num">Score</th></tr></thead>
                  <tbody>
                    {shown.map(l => (
                      <tr key={l.id} className={l.id === current ? "on" : ""} onClick={() => onSelectLift(l.id)}>
                        <td>{liftLabel(l)}</td><td>{l.operator}</td>
                        <td className="num">{l.liftedKg ? `${(l.liftedKg * 1000).toFixed(0)} g` : "—"}</td>
                        <td className="num">{l.durationS !== null ? fmtS(l.durationS) : "—"}</td>
                        <td className="num">{l.score?.toFixed(0)} <span className={`dl-grade g${l.grade}`}>{l.grade}</span></td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
              <div className="dl-card">
                <div className="dl-card-head"><span className="dl-card-title">Per operator</span></div>
                <table className="dl-table">
                  <thead><tr><th>Operator</th><th className="num">Lifts</th><th className="num">Avg score</th><th className="num">Best</th><th className="num">Avg time</th></tr></thead>
                  <tbody>
                    {perOp.map(p => (
                      <tr key={p.op}><td>{p.op}</td><td className="num">{p.n}</td><td className="num">{p.avgScore.toFixed(1)}</td>
                        <td className="num">{p.best.toFixed(0)}</td><td className="num">{fmtS(p.avgDur)}</td></tr>
                    ))}
                  </tbody>
                </table>
              </div>
            </div>
          )}
        </div>
      )}
    </div>
  );
}
