import { useEffect, useMemo, useState } from "react";
import { Printer, RefreshCcw, SlidersHorizontal } from "lucide-react";
import type { LiftReport, LiftSummary } from "../../types";
import { fetchLifts, fetchReport, liftLabel } from "../datalogger/DataLoggerTab";
import { PhaseBar } from "../datalogger/LiftReview";
import { fmtS } from "../datalogger/chartKit";
import "../datalogger/datalogger.css";

const API_BASE = "http://localhost:8000/api";

function money(v: number, currency: string): string {
  const sym = currency === "INR" ? "₹" : currency + " ";
  return `${sym}${v.toLocaleString(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 2 })}`;
}

const PEN_LABEL: Record<string, string> = {
  cutEvent: "Each entry into CUT", warnEvent: "Each entry into WARN", cutPerS: "Per second in CUT",
  warnPerS: "Per second in WARN", snatch: "Each snatch pick-up", tipWarnPerS: "Per second near tipping",
  refusal: "Each motion refused by SLI", estop: "Each E-stop", sliNotArmed: "Lift with SLI not armed",
};

function ScoreBar({ label, value, max, note }: { label: string; value: number; max: number; note: string }) {
  return (
    <div className="op-bar">
      <div className="op-bar-top"><span>{label}</span><b>{value.toFixed(1)} / {max}</b></div>
      <div className="op-bar-track"><span style={{ width: `${(value / max) * 100}%` }} /></div>
      <div className="dl-muted">{note}</div>
    </div>
  );
}

function Metric({ label, value, hint }: { label: string; value: string; hint?: string }) {
  return (
    <div className="dl-stat" title={hint}>
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
  const num = (k: string, label: string, step = 1) => (
    <label className="op-field" key={k}>
      <span>{label}</span>
      <input className="form-input" type="number" step={step} value={s[k] as number}
             onChange={e => setS({ ...s, [k]: Number(e.target.value) })} />
    </label>
  );
  const save = async () => {
    const r = await fetch(`${API_BASE}/lift/settings`, {
      method: "PUT", headers: { "Content-Type": "application/json" }, body: JSON.stringify(s),
    }).catch(() => null);
    setMsg(r?.ok ? "Saved. Re-score lifts to apply." : "Save failed");
    if (r?.ok) onSaved();
  };
  return (
    <div className="op-settings">
      <div className="op-settings-col">
        <div className="section-label">Cost rates ({String(s.currency)})</div>
        {num("ratePerMin", "Per minute of lift")}
        {num("ratePerKg", "Per kg lifted")}
        {num("ratePerPenaltyPoint", "Per penalty point")}
        <div className="section-label" style={{ marginTop: 8 }}>Scoring</div>
        {num("safetyZeroAt", "Penalty points for 0 safety score")}
        {num("snatchDaf", "Snatch above DAF", 0.05)}
      </div>
      <div className="op-settings-col">
        <div className="section-label">Penalty points</div>
        {Object.keys(pen).map(k => (
          <label className="op-field" key={k}>
            <span>{PEN_LABEL[k] ?? k}</span>
            <input className="form-input" type="number" step={0.5} value={pen[k]}
                   onChange={e => setS({ ...s, pen: { ...pen, [k]: Number(e.target.value) } })} />
          </label>
        ))}
      </div>
      <div className="op-settings-actions">
        <button className="btn-primary" onClick={save}>Save settings</button>
        {msg && <span className="dl-muted">{msg}</span>}
      </div>
    </div>
  );
}

export function OperatorTab({ selectedLift, onSelectLift, liftsVersion }: {
  selectedLift: string | null; onSelectLift: (id: string | null) => void; liftsVersion: number;
}) {
  const [lifts, setLifts] = useState<LiftSummary[]>([]);
  const [report, setReport] = useState<LiftReport | null>(null);
  const [who, setWho] = useState("");
  const [showSettings, setShowSettings] = useState(false);
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
             avgCost: avg(l => l.cost ?? 0), avgDur: avg(l => l.durationS ?? 0) };
  }).filter(r => r.n), [lifts, operators]);

  const rescore = async (all: boolean) => {
    setBusy(true);
    const ids = all ? lifts.map(l => l.id) : current ? [current] : [];
    for (const id of ids) await fetch(`${API_BASE}/lifts/${id}/reanalyse`, { method: "POST" }).catch(() => null);
    setBusy(false);
    setBump(b => b + 1);
  };

  const r = report?.ok ? report : null;

  return (
    <div className="dl-page op-page">
      <div className="dl-topbar no-print">
        <label className="dl-filter">
          Lift
          <select className="form-select dl-select wide" value={current ?? ""} onChange={e => onSelectLift(e.target.value || null)}>
            {lifts.length === 0 && <option value="">No lifts recorded</option>}
            {lifts.map(l => (
              <option key={l.id} value={l.id}>
                {l.operator} · {liftLabel(l)}{l.grade ? ` · ${l.grade} ${l.score?.toFixed(0)}` : ""}
              </option>
            ))}
          </select>
        </label>
        <span className="dl-card-fill" />
        <button className="dl-icon-btn" onClick={() => setShowSettings(v => !v)}><SlidersHorizontal size={11} /> Score &amp; cost settings</button>
        <button className="dl-icon-btn" disabled={busy || !current} onClick={() => rescore(false)} title="Re-run the analysis with the current settings">
          <RefreshCcw size={11} /> Re-score
        </button>
        <button className="dl-icon-btn" disabled={busy || !lifts.length} onClick={() => rescore(true)}>Re-score all</button>
        <button className="dl-icon-btn" disabled={!r} onClick={() => window.print()} title="Print or save the report as PDF">
          <Printer size={11} /> Print / PDF
        </button>
      </div>

      {showSettings && <div className="dl-card no-print"><SettingsEditor onSaved={() => setBump(b => b + 1)} /></div>}

      {!current && <div className="dl-empty">No lifts yet. Pick an operator in the header, press Start lift, do the lift, press Stop lift.</div>}
      {current && !report && <div className="dl-empty">Loading report…</div>}
      {report && !report.ok && <div className="dl-empty">{report.reason}</div>}

      {r && (
        <div className="op-report">
          <div className="dl-card op-head">
            <div className={`op-grade g${r.score.grade}`}>
              <span className="op-grade-letter">{r.score.grade}</span>
              <span className="op-grade-num">{r.score.total.toFixed(0)}</span>
            </div>
            <div className="op-head-text">
              <div className="op-title">Operator report: {r.operator}</div>
              <div className="dl-muted">
                {new Date(r.startedAt).toLocaleString()} · {fmtS(r.durationS)} · lifted {(r.load.liftedKg * 1000).toFixed(0)} g
                {r.load.ratedKg ? ` of ${(r.load.ratedKg * 1000).toFixed(0)} g rated` : ""} · {r.id}
              </div>
              {r.note && <div className="dl-muted">{r.note}</div>}
            </div>
            <div className="op-cost">
              <span className="dl-stat-label">Lift cost</span>
              <span className="op-cost-total">{money(r.cost.total, r.cost.currency)}</span>
            </div>
          </div>

          <div className="op-cols">
            <div className="dl-card">
              <div className="dl-card-head"><span className="dl-card-title">Score</span></div>
              <ScoreBar label="Safety" value={r.score.safety} max={r.score.max.safety}
                        note={`${r.penalty.points.toFixed(1)} penalty points`} />
              <ScoreBar label="Smoothness" value={r.score.smoothness} max={r.score.max.smoothness}
                        note={`DAF ${r.smoothness.dafMax.toFixed(2)} · ripple ${r.smoothness.meanRippleG.toFixed(1)} g · ${r.smoothness.reversalsPerMin} reversals/min`} />
              <ScoreBar label="Efficiency" value={r.score.efficiency} max={r.score.max.efficiency}
                        note={`${(r.efficiency.idleFrac * 100).toFixed(0)} % idle · ${r.smoothness.jogsPerMin} motion starts/min`} />
            </div>

            <div className="dl-card">
              <div className="dl-card-head"><span className="dl-card-title">Cost breakdown</span></div>
              <table className="dl-table">
                <tbody>
                  <tr><td>Time</td><td>{(r.durationS / 60).toFixed(2)} min × {money(r.cost.rates.perMin, r.cost.currency)}</td><td>{money(r.cost.time, r.cost.currency)}</td></tr>
                  <tr><td>Weight lifted</td><td>{r.load.liftedKg.toFixed(3)} kg × {money(r.cost.rates.perKg, r.cost.currency)}</td><td>{money(r.cost.load, r.cost.currency)}</td></tr>
                  <tr><td>Safety penalty</td><td>{r.penalty.points.toFixed(1)} pts × {money(r.cost.rates.perPenaltyPoint, r.cost.currency)}</td><td>{money(r.cost.penalty, r.cost.currency)}</td></tr>
                  <tr className="total"><td>Total</td><td /><td>{money(r.cost.total, r.cost.currency)}</td></tr>
                </tbody>
              </table>
              <div className="dl-card-head" style={{ marginTop: 10 }}><span className="dl-card-title">Penalty points</span></div>
              <table className="dl-table">
                <tbody>
                  {r.penalty.items.length === 0 && <tr><td className="dl-muted" colSpan={3}>None. Clean lift.</td></tr>}
                  {r.penalty.items.map(i => (
                    <tr key={i.what}><td>{i.what}</td><td>{i.value}</td><td>{i.points.toFixed(1)}</td></tr>
                  ))}
                </tbody>
              </table>
            </div>

            <div className="dl-card">
              <div className="dl-card-head"><span className="dl-card-title">Key numbers</span></div>
              <div className="dl-now-grid">
                <Metric label="Peak load %" value={r.load.peakPct !== null ? `${r.load.peakPct} %` : "—"} />
                <Metric label="Mean load %" value={r.load.meanPct !== null ? `${r.load.meanPct} %` : "—"} />
                <Metric label="Peak DAF" value={r.smoothness.dafMax.toFixed(2)} hint="Dynamic amplification at pick-up: peak / static load" />
                <Metric label="Time in WARN" value={`${r.safety.warnS} s`} />
                <Metric label="Time in CUT" value={`${r.safety.cutS} s`} />
                <Metric label="Min tipping margin" value={r.safety.minTipMargin !== null ? `${r.safety.minTipMargin} %` : "—"} />
                <Metric label="Swing-stop ripple" value={r.smoothness.swingStopRippleG !== null ? `${r.smoothness.swingStopRippleG} g` : "—"}
                        hint="Load-cell ripple in the 2 s after the swing stops (sway proxy)" />
                <Metric label="Refused motions" value={String(r.safety.refusals)} />
                <Metric label="SLI mode" value={r.safety.modes.join(", ")} />
              </div>
            </div>
          </div>

          <div className="dl-card">
            <div className="dl-card-head"><span className="dl-card-title">Lift phases</span></div>
            <PhaseBar phases={r.phases} duration={r.durationS || 1} />
            <div className="op-phase-totals">
              {Object.entries(r.phaseTotals).map(([k, v]) => (
                <span key={k}><b>{k}</b> {fmtS(v)} ({((v / (r.durationS || 1)) * 100).toFixed(0)} %)</span>
              ))}
            </div>
          </div>

          <div className="dl-card">
            <div className="dl-card-head"><span className="dl-card-title">Flagged moments</span></div>
            {r.flags.length === 0 ? <div className="dl-muted">Nothing flagged.</div> : (
              <ul className="op-flags">
                {r.flags.map((f, i) => (
                  <li key={i} className={f.level}>
                    <span className="op-flag-tag">{f.level === "crit" ? "CRIT" : f.level === "warn" ? "WARN" : "INFO"}</span>
                    <span className="op-flag-t">{fmtS(f.t)}</span>{f.text}
                  </li>
                ))}
              </ul>
            )}
          </div>
        </div>
      )}

      {lifts.length > 0 && (
        <div className="op-cols two no-print">
          <div className="dl-card">
            <div className="dl-card-head">
              <span className="dl-card-title">All lifts</span>
              <span className="dl-card-fill" />
              <select className="form-select dl-select" value={who} onChange={e => setWho(e.target.value)}>
                <option value="">All operators</option>
                {operators.map(o => <option key={o} value={o}>{o}</option>)}
              </select>
            </div>
            <table className="dl-table clickable">
              <thead><tr><th>When</th><th>Operator</th><th>Load</th><th>Time</th><th>Score</th><th>Cost</th></tr></thead>
              <tbody>
                {shown.map(l => (
                  <tr key={l.id} className={l.id === current ? "on" : ""} onClick={() => onSelectLift(l.id)}>
                    <td>{liftLabel(l)}</td><td>{l.operator}</td>
                    <td>{l.liftedKg ? `${(l.liftedKg * 1000).toFixed(0)} g` : "—"}</td>
                    <td>{l.durationS !== null ? fmtS(l.durationS) : "—"}</td>
                    <td>{l.score?.toFixed(0)} <span className={`dl-grade g${l.grade}`}>{l.grade}</span></td>
                    <td>{l.cost !== null ? l.cost.toFixed(0) : "—"}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
          <div className="dl-card">
            <div className="dl-card-head"><span className="dl-card-title">Per operator</span></div>
            <table className="dl-table">
              <thead><tr><th>Operator</th><th>Lifts</th><th>Avg score</th><th>Best</th><th>Avg time</th><th>Avg cost</th></tr></thead>
              <tbody>
                {perOp.map(p => (
                  <tr key={p.op}><td>{p.op}</td><td>{p.n}</td><td>{p.avgScore.toFixed(1)}</td><td>{p.best.toFixed(0)}</td>
                    <td>{fmtS(p.avgDur)}</td><td>{p.avgCost.toFixed(0)}</td></tr>
                ))}
              </tbody>
            </table>
          </div>
        </div>
      )}
    </div>
  );
}
