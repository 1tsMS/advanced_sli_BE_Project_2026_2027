import { useEffect, useMemo, useState } from "react";
import {
  LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer, ReferenceArea, ReferenceLine,
  Brush, ScatterChart, Scatter, ZAxis,
} from "recharts";
import { FileDown } from "lucide-react";
import type { LiftReport, LiftRow, LiftPhase } from "../../types";
import { C, AXIS_TICK, ChartCard, TipBox, PHASE_COLOR, fmtS } from "./chartKit";

const API_BASE = "http://localhost:8000/api";
const SERIES_POINTS = 1500;

type StateFilter = "all" | "alarm";

/** Time spans where the SLI state is WARN or CUT (for shading). */
function stateSpans(rows: LiftRow[]): { t0: number; t1: number; state: string }[] {
  const out: { t0: number; t1: number; state: string }[] = [];
  let prev = "";
  for (const r of rows) {
    const s = r.state === "WARN" || r.state === "CUT" ? r.state : "";
    if (s && s === prev) out[out.length - 1].t1 = r.t;     // span continues
    else if (s) out.push({ t0: r.t, t1: r.t, state: s });  // new span
    prev = s;
  }
  return out;
}

export function PhaseBar({ phases, duration, range }: { phases: LiftPhase[]; duration: number; range?: [number, number] }) {
  const names = Array.from(new Set(phases.map(p => p.name)));
  return (
    <div className="dl-phasebar-wrap">
      <div className="dl-phasebar">
        {phases.map((p, i) => (
          <span key={i} className="dl-phase" title={`${p.name}: ${p.t0.toFixed(1)} - ${p.t1.toFixed(1)} s`}
                style={{ left: `${(p.t0 / duration) * 100}%`, width: `${Math.max(0.3, ((p.t1 - p.t0) / duration) * 100)}%`,
                         background: PHASE_COLOR[p.name] ?? C.ref }} />
        ))}
        {range && (
          <span className="dl-phase-range" style={{ left: `${(range[0] / duration) * 100}%`,
                width: `${((range[1] - range[0]) / duration) * 100}%` }} />
        )}
      </div>
      <div className="dl-legend">
        {names.map(n => (
          <span key={n} className="dl-legend-item">
            <span className="dl-swatch" style={{ background: PHASE_COLOR[n] ?? C.ref }} />{n}
          </span>
        ))}
      </div>
    </div>
  );
}

export function LiftReview({ liftId, report }: { liftId: string; report: LiftReport | null }) {
  const [rows, setRows] = useState<LiftRow[] | null>(null);
  const [total, setTotal] = useState(0);
  const [err, setErr] = useState("");
  const [range, setRange] = useState<[number, number] | null>(null);
  const [filter, setFilter] = useState<StateFilter>("all");
  const [phase, setPhase] = useState<string>("");

  useEffect(() => {
    let alive = true;
    setRows(null); setErr(""); setRange(null); setPhase(""); setFilter("all");
    fetch(`${API_BASE}/lifts/${liftId}/series?n=${SERIES_POINTS}`)
      .then(r => r.ok ? r.json() : Promise.reject(r.status))
      .then(d => { if (alive) { setRows(d.rows); setTotal(d.total); } })
      .catch(() => alive && setErr("Could not load this lift's data"));
    return () => { alive = false; };
  }, [liftId]);

  const duration = rows?.length ? rows[rows.length - 1].t : 1;
  const view = useMemo(() => {
    if (!rows) return [];
    let v = range ? rows.filter(r => r.t >= range[0] && r.t <= range[1]) : rows;
    if (filter === "alarm") {
      v = v.map(r => (r.state === "WARN" || r.state === "CUT") ? r
        : { ...r, load: null as unknown as number, pct: null, boom: null, swing: null, ext: null, hookDrop: null, tipMargin: null });
    }
    // commands as lanes: swing 4, boom 3, telescope 2, winch 1, +-0.3 for direction
    return v.map(r => ({ ...r, c1: 4 + 0.3 * r.m1, c2: 3 + 0.3 * r.m2, c3: 2 + 0.3 * r.m3, c4: 1 + 0.3 * r.m4,
                          tip: r.tipValid ? r.tipMargin : null }));
  }, [rows, range, filter]);
  const spans = useMemo(() => (rows ? stateSpans(rows) : []), [rows]);
  const phases = report?.ok ? report.phases : [];
  const xDomain: [number, number] = range ?? [0, duration];
  const hasTip = !!rows?.some(r => r.tipValid);
  const warnPct = 90, cutPct = 100;

  const pickPhase = (name: string) => {
    setPhase(name);
    if (!name) { setRange(null); return; }
    const ps = phases.filter(p => p.name === name);
    if (ps.length) setRange([Math.max(0, ps[0].t0 - 0.5), Math.min(duration, ps[ps.length - 1].t1 + 0.5)]);
  };

  if (err) return <div className="dl-empty">{err}</div>;
  if (!rows) return <div className="dl-empty">Loading lift…</div>;
  if (!rows.length) return <div className="dl-empty">This lift has no telemetry rows.</div>;

  const X = (
    <XAxis dataKey="t" type="number" domain={xDomain} allowDataOverflow tick={AXIS_TICK} stroke={C.grid}
           tickFormatter={v => `${(+v).toFixed(0)}s`} />
  );
  const bands = (
    <>
      {phases.filter(p => p.name === "pick-up" || p.name === "set-down").map((p, i) => (
        <ReferenceArea key={`p${i}`} x1={p.t0} x2={p.t1} fill={PHASE_COLOR[p.name]} fillOpacity={0.10} ifOverflow="hidden" />
      ))}
      {spans.map((s, i) => (
        <ReferenceArea key={`s${i}`} x1={s.t0} x2={Math.max(s.t1, s.t0 + 0.05)} fill={s.state === "CUT" ? C.crit : C.warn}
                       fillOpacity={0.16} ifOverflow="hidden" />
      ))}
    </>
  );
  const margin = { top: 6, right: 12, bottom: 0, left: -6 };

  return (
    <div className="dl-review">
      <div className="dl-filters">
        <label className="dl-filter">
          Phase
          <select className="form-select dl-select" value={phase} onChange={e => pickPhase(e.target.value)}>
            <option value="">Whole lift</option>
            {Array.from(new Set(phases.map(p => p.name))).map(n => <option key={n} value={n}>{n}</option>)}
          </select>
        </label>
        <div className="dl-seg" role="group" aria-label="Samples">
          <button className={filter === "all" ? "on" : ""} onClick={() => setFilter("all")}>All samples</button>
          <button className={filter === "alarm" ? "on" : ""} onClick={() => setFilter("alarm")}
                  title="Show only samples where the SLI was in WARN or CUT">Only WARN / CUT</button>
        </div>
        {range && (
          <button className="dl-icon-btn" onClick={() => { setRange(null); setPhase(""); }}>
            Reset zoom ({range[0].toFixed(1)}–{range[1].toFixed(1)} s)
          </button>
        )}
        <span className="dl-card-fill" />
        <span className="dl-muted">{total.toLocaleString()} samples · showing {rows.length < total ? `${rows.length} (LTTB)` : "all"}</span>
        <a className="dl-icon-btn" href={`${API_BASE}/logs/${liftId}.csv`} download title="Raw CSV, every sample">
          <FileDown size={11} /> CSV
        </a>
      </div>

      {phases.length > 0 && <PhaseBar phases={phases} duration={duration} range={range ?? undefined} />}

      <ChartCard title="Load (drag the handles to zoom every chart)" height={150} exportable={false}>
        <ResponsiveContainer>
          <LineChart data={rows} margin={margin}>
            <CartesianGrid stroke={C.grid} vertical={false} />
            <XAxis dataKey="t" type="number" domain={[0, duration]} tick={AXIS_TICK} stroke={C.grid} tickFormatter={v => `${(+v).toFixed(0)}s`} />
            <YAxis tick={AXIS_TICK} stroke={C.grid} unit=" kg" width={58} />
            <Line dataKey="load" name="Load" stroke={C.s1} strokeWidth={1.5} dot={false} isAnimationActive={false} />
            <Brush dataKey="t" height={18} stroke={C.ref} fill={C.surface} travellerWidth={8}
                   tickFormatter={v => `${(+v).toFixed(0)}s`}
                   onChange={({ startIndex, endIndex }) => {
                     if (startIndex === undefined || endIndex === undefined) return;
                     if (startIndex === 0 && endIndex === rows.length - 1) setRange(null);
                     else setRange([rows[startIndex].t, rows[endIndex].t]);
                   }} />
          </LineChart>
        </ResponsiveContainer>
      </ChartCard>

      <div className="dl-grid">
        <ChartCard title="Load vs rated capacity" height={220}
                   legend={[{ label: "Load", color: C.s1 }, { label: "Rated capacity", color: C.ref, dashed: true },
                            { label: "WARN / CUT", color: C.warn, band: true }]}>
          <ResponsiveContainer>
            <LineChart data={view} margin={margin} syncId="lift">
              <CartesianGrid stroke={C.grid} vertical={false} />
              {X}
              <YAxis tick={AXIS_TICK} stroke={C.grid} unit=" kg" width={58} domain={[0, "auto"]} />
              {bands}
              {report?.ok && report.pickups.map((p, i) => (
                <ReferenceLine key={i} x={p.t} stroke={p.snatch ? C.crit : C.ref} strokeDasharray="2 3"
                               label={{ value: `DAF ${p.daf.toFixed(2)}`, fill: C.axis, fontSize: 10, position: "insideTopRight" }} />
              ))}
              <Tooltip content={<TipBox unit={{ Load: " kg", "Rated capacity": " kg" }} />} isAnimationActive={false} />
              <Line dataKey="capKg" name="Rated capacity" stroke={C.ref} strokeDasharray="5 4" strokeWidth={1.5} dot={false} isAnimationActive={false} />
              <Line dataKey="load" name="Load" stroke={C.s1} strokeWidth={2} dot={false} isAnimationActive={false} />
            </LineChart>
          </ResponsiveContainer>
        </ChartCard>

        <ChartCard title="Load % of capacity" height={220}>
          <ResponsiveContainer>
            <LineChart data={view} margin={margin} syncId="lift">
              <CartesianGrid stroke={C.grid} vertical={false} />
              {X}
              <YAxis tick={AXIS_TICK} stroke={C.grid} unit="%" width={52} domain={[0, (max: number) => Math.max(120, Math.ceil(Math.min(max, 300) / 20) * 20)]} allowDataOverflow />
              {bands}
              <ReferenceLine y={warnPct} stroke={C.warn} strokeDasharray="3 3" />
              <ReferenceLine y={cutPct} stroke={C.crit} strokeDasharray="3 3" />
              <Tooltip content={<TipBox unit={{ "Load %": "%" }} />} isAnimationActive={false} />
              <Line dataKey="pct" name="Load %" stroke={C.s1} strokeWidth={2} dot={false} isAnimationActive={false} />
            </LineChart>
          </ResponsiveContainer>
        </ChartCard>

        <ChartCard title="Boom and swing angle" height={200}
                   legend={[{ label: "Boom", color: C.s1 }, { label: "Swing", color: C.s2 }]}>
          <ResponsiveContainer>
            <LineChart data={view} margin={margin} syncId="lift">
              <CartesianGrid stroke={C.grid} vertical={false} />
              {X}
              <YAxis tick={AXIS_TICK} stroke={C.grid} unit="°" width={52} />
              <Tooltip content={<TipBox unit={{ Boom: "°", Swing: "°" }} />} isAnimationActive={false} />
              <Line dataKey="boom" name="Boom" stroke={C.s1} strokeWidth={2} dot={false} isAnimationActive={false} />
              <Line dataKey="swing" name="Swing" stroke={C.s2} strokeWidth={2} dot={false} isAnimationActive={false} />
            </LineChart>
          </ResponsiveContainer>
        </ChartCard>

        <ChartCard title="Telescope extension and hook drop" height={200}
                   legend={[{ label: "Extension", color: C.s1 }, { label: "Hook drop", color: C.s3 }]}>
          <ResponsiveContainer>
            <LineChart data={view} margin={margin} syncId="lift">
              <CartesianGrid stroke={C.grid} vertical={false} />
              {X}
              <YAxis tick={AXIS_TICK} stroke={C.grid} unit=" mm" width={60} />
              <Tooltip content={<TipBox unit={{ Extension: " mm", "Hook drop": " mm" }} />} isAnimationActive={false} />
              <Line dataKey="ext" name="Extension" stroke={C.s1} strokeWidth={2} dot={false} isAnimationActive={false} />
              <Line dataKey="hookDrop" name="Hook drop" stroke={C.s3} strokeWidth={2} dot={false} isAnimationActive={false} />
            </LineChart>
          </ResponsiveContainer>
        </ChartCard>

        <ChartCard title="Operator commands" height={190}>
          <ResponsiveContainer>
            <LineChart data={view} margin={margin} syncId="lift">
              <CartesianGrid stroke={C.grid} vertical={false} />
              {X}
              <YAxis tick={AXIS_TICK} stroke={C.grid} width={58} domain={[0.4, 4.6]} ticks={[1, 2, 3, 4]}
                     tickFormatter={v => ({ 1: "winch", 2: "extend", 3: "boom", 4: "swing" } as Record<number, string>)[v] ?? ""} />
              <Line dataKey="c1" name="Swing" stroke={C.s1} strokeWidth={1.5} dot={false} type="stepAfter" isAnimationActive={false} />
              <Line dataKey="c2" name="Boom" stroke={C.s2} strokeWidth={1.5} dot={false} type="stepAfter" isAnimationActive={false} />
              <Line dataKey="c3" name="Extend" stroke={C.s3} strokeWidth={1.5} dot={false} type="stepAfter" isAnimationActive={false} />
              <Line dataKey="c4" name="Winch" stroke={C.ref} strokeWidth={1.5} dot={false} type="stepAfter" isAnimationActive={false} />
            </LineChart>
          </ResponsiveContainer>
        </ChartCard>

        <ChartCard title="Tipping margin" height={190}>
          {hasTip ? (
            <ResponsiveContainer>
              <LineChart data={view} margin={margin} syncId="lift">
                <CartesianGrid stroke={C.grid} vertical={false} />
                {X}
                <YAxis tick={AXIS_TICK} stroke={C.grid} unit="%" width={52} domain={[0, 100]} />
                <ReferenceArea y1={0} y2={30} fill={C.crit} fillOpacity={0.08} />
                <Tooltip content={<TipBox unit={{ "Tipping margin": "%" }} />} isAnimationActive={false} />
                <Line dataKey="tip" name="Tipping margin" stroke={C.s3} strokeWidth={2} dot={false} isAnimationActive={false} />
              </LineChart>
            </ResponsiveContainer>
          ) : <div className="dl-empty small">No FSR readings in this lift.</div>}
        </ChartCard>

        <ChartCard title="Operating points: load vs boom angle" height={220}>
          <ResponsiveContainer>
            <ScatterChart margin={{ top: 6, right: 12, bottom: 0, left: -6 }}>
              <CartesianGrid stroke={C.grid} />
              <XAxis dataKey="boom" type="number" name="Boom" unit="°" tick={AXIS_TICK} stroke={C.grid} />
              <YAxis dataKey="load" type="number" name="Load" unit=" kg" tick={AXIS_TICK} stroke={C.grid} width={58} />
              <ZAxis range={[14, 14]} />
              <Tooltip cursor={{ stroke: C.ref }} isAnimationActive={false}
                       formatter={(v) => (typeof v === "number" ? v.toFixed(3) : String(v))} />
              <Scatter data={view.filter(r => r.load !== null && (r.load ?? 0) > 0.02)} fill={C.s1} fillOpacity={0.5} isAnimationActive={false} />
            </ScatterChart>
          </ResponsiveContainer>
        </ChartCard>

        {report?.ok && (
          <div className="dl-card">
            <div className="dl-card-head"><span className="dl-card-title">Pick-ups and stops</span></div>
            <table className="dl-table">
              <thead><tr><th>t</th><th>Event</th><th>Value</th></tr></thead>
              <tbody>
                {[
                  ...report.pickups.map(p => ({ t: p.t, bad: p.snatch, what: `Pick-up ${(p.staticKg * 1000).toFixed(0)} g`,
                                                val: `DAF ${p.daf.toFixed(2)}${p.snatch ? " (snatch)" : ""}` })),
                  ...report.smoothness.stops.map(s => ({ t: s.t, bad: false, what: `${s.axis} stop${s.shaped ? " (shaped)" : ""}`,
                                                         val: `ripple ${s.rippleG.toFixed(1)} g` })),
                ].sort((a, b) => a.t - b.t).map((e, i) => (
                  <tr key={i} className={e.bad ? "bad" : ""}><td>{fmtS(e.t)}</td><td>{e.what}</td><td>{e.val}</td></tr>
                ))}
                {!report.pickups.length && !report.smoothness.stops.length && (
                  <tr><td colSpan={3} className="dl-muted">No pick-ups or loaded stops.</td></tr>
                )}
              </tbody>
            </table>
          </div>
        )}
      </div>
    </div>
  );
}
