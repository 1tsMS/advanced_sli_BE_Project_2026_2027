import { useEffect, useState, type RefObject } from "react";
import {
  LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer, ReferenceLine, ReferenceArea,
} from "recharts";
import { Play, Square } from "lucide-react";
import type { LivePoint, Capture } from "../../types";
import { C, AXIS_TICK, ChartCard, ChartGrid, TipBox, fmtS } from "./chartKit";
import { stableDomain, ticksFor } from "../telemetry/shock";

const mmss = (v: number) => `${Math.floor(v / 60)}:${String(Math.round(v % 60)).padStart(2, "0")}`;

const REFRESH_MS = 500;       // 2 Hz redraw: 7 SVG charts cost ~25 ms per 100 points each time
const MAX_POINTS = 240;       // per chart (~1 per 2 px); longer spans are thinned (peaks kept, see thin())
const CAPTURE_S = 300;

type Mode = "rolling" | "capture";

/**
 * Every k-th sample, but each kept sample carries the PEAK accel / jerk of its
 * bucket, so a 20 ms shock survives thinning a 5-min capture to 600 points.
 */
function thin(src: LivePoint[], toT: (p: LivePoint) => number): LivePoint[] {
  if (!src.length) return [];
  const step = Math.max(1, Math.ceil(src.length / MAX_POINTS));
  const out: LivePoint[] = [];
  for (let i = 0; i < src.length; i += step) {
    let accel = 0, jerk = 0;
    for (let k = i; k < Math.min(i + step, src.length); k++) {
      accel = Math.max(accel, src[k].accel);
      jerk = Math.max(jerk, src[k].jerk);
    }
    out.push({ ...src[i], accel, jerk, t: +toT(src[i]).toFixed(2) });
  }
  const last = src[src.length - 1];
  if (out[out.length - 1].ts !== last.ts) out.push({ ...last, t: +toT(last).toFixed(2) });
  return out;
}

export function LiveCharts({ history, capture, onStartCapture, onStopCapture, warnPct = 90, cutPct = 100, tipWarnPct = 30 }: {
  history: RefObject<LivePoint[]>;
  capture: RefObject<Capture>;
  onStartCapture: () => void;
  onStopCapture: () => void;
  warnPct?: number; cutPct?: number; tipWarnPct?: number;
}) {
  const [data, setData] = useState<LivePoint[]>([]);
  const [paused, setPaused] = useState(false);
  const [mode, setMode] = useState<Mode>("rolling");
  const [cap, setCap] = useState<{ active: boolean; elapsed: number }>({ active: false, elapsed: 0 });

  useEffect(() => {
    if (paused) return;
    const tick = () => {
      if (mode === "rolling") {
        const src = history.current ?? [];
        const now = src.length ? src[src.length - 1].ts : 0;
        setData(thin(src, p => p.ts - now));
      } else {
        const c = capture.current!;
        setData(thin(c.points, p => p.ts - c.t0));
        const last = c.points[c.points.length - 1];
        setCap({ active: c.active, elapsed: last ? last.ts - c.t0 : 0 });
      }
    };
    tick();
    const id = setInterval(tick, REFRESH_MS);
    return () => clearInterval(id);
  }, [history, capture, paused, mode]);

  // Fixed x-range per mode: the plot never re-zooms as data arrives
  const xDomain: [number, number] = mode === "rolling" ? [-60, 0] : [0, CAPTURE_S];
  const xTicks = mode === "rolling" ? [-60, -50, -40, -30, -20, -10, 0] : [0, 60, 120, 180, 240, 300];
  const X = (
    <XAxis dataKey="t" type="number" domain={xDomain} ticks={xTicks} allowDataOverflow
           tick={AXIS_TICK} stroke={C.grid} tickFormatter={v => (mode === "rolling" ? `${v}s` : mmss(+v))} />
  );

  // Y-ranges that only move in whole steps and never shrink below a useful span
  const pctTop = Math.max(120, Math.ceil(Math.max(0, ...data.map(d => d.pct ?? 0)) / 20) * 20);
  const loadDom = stableDomain(data.flatMap(d => [d.load, d.cap]), 0.5, 0.25, true);
  const angDom = stableDomain(data.flatMap(d => [d.boom, d.swing]), 10, 5);
  const extDom = stableDomain(data.flatMap(d => [d.ext, d.hookDrop]), 50, 25, true);
  const accDom = stableDomain(data.map(d => d.accel), 1, 0.5, true);
  const jerkDom = stableDomain(data.map(d => d.jerk), 50, 25, true);

  const last = data[data.length - 1];
  const hasTip = data.some(d => d.tip !== null);
  const margin = { top: 6, right: 12, bottom: 0, left: -6 };

  const toolbar = (
    <div className="dl-filters">
      <div className="dl-seg" role="radiogroup" aria-label="Chart time span">
        <button className={mode === "rolling" ? "on" : ""} onClick={() => setMode("rolling")} role="radio" aria-checked={mode === "rolling"}
                title="The last 60 s, scrolling">Rolling 60 s</button>
        <button className={mode === "capture" ? "on" : ""} onClick={() => setMode("capture")} role="radio" aria-checked={mode === "capture"}
                title="A fixed 0-5 min chart that fills up from when you press Start">5-min capture</button>
      </div>
      {mode === "capture" && (
        cap.active
          ? <button className="dl-icon-btn on" onClick={onStopCapture}><Square size={13} /> Stop · {fmtS(cap.elapsed)}</button>
          : <button className="btn-primary btn-sm" onClick={() => { onStartCapture(); setPaused(false); }}>
              <Play size={13} /> {capture.current?.points.length ? "Start again" : "Start 5-min capture"}
            </button>
      )}
      {mode === "capture" && !cap.active && cap.elapsed > 0 && (
        <span className="dl-muted">Captured {fmtS(cap.elapsed)}. Export any chart as PNG / SVG.</span>
      )}
      <span className="dl-card-fill" />
      <button className={`dl-icon-btn ${paused ? "on" : ""}`} onClick={() => setPaused(p => !p)}
              title="Freeze the charts (to read or export them)">
        {paused ? "Resume" : "Pause"}
      </button>
    </div>
  );

  if (!data.length) {
    return (
      <>
        {toolbar}
        <div className="dl-empty">
          {mode === "rolling" ? "No telemetry yet. Connect the ESP32 (Settings) and the last 60 s show up here."
                              : "Press Start: the charts fill from 0 to 5 min, then stop by themselves."}
        </div>
      </>
    );
  }

  return (
    <>
      {toolbar}
      <ChartGrid>
        <ChartCard title="Load vs rated capacity" height={210}
                   legend={[{ label: "Load", color: C.s1 }, { label: "Rated capacity", color: C.ref, dashed: true }]}>
          <ResponsiveContainer>
            <LineChart data={data} margin={margin}>
              <CartesianGrid stroke={C.grid} vertical={false} />
              {X}
              <YAxis tick={AXIS_TICK} stroke={C.grid} unit=" kg" width={76} domain={loadDom} ticks={ticksFor(loadDom, 0.25)} tickFormatter={v => (+v).toFixed(2)} />
              <Tooltip content={<TipBox unit={{ Load: " kg", "Rated capacity": " kg" }} />} isAnimationActive={false} />
              <Line dataKey="cap" name="Rated capacity" stroke={C.ref} strokeDasharray="5 4" strokeWidth={1.5} dot={false} isAnimationActive={false} />
              <Line dataKey="load" name="Load" stroke={C.s1} strokeWidth={2} dot={false} isAnimationActive={false} />
            </LineChart>
          </ResponsiveContainer>
        </ChartCard>

        <ChartCard title="Load % of capacity" height={210}>
          <ResponsiveContainer>
            <LineChart data={data} margin={margin}>
              <CartesianGrid stroke={C.grid} vertical={false} />
              {X}
              <YAxis tick={AXIS_TICK} stroke={C.grid} unit="%" width={56} domain={[0, pctTop]} ticks={ticksFor([0, pctTop], 20)} allowDataOverflow />
              <ReferenceArea y1={warnPct} y2={cutPct} fill={C.warn} fillOpacity={0.08} />
              <ReferenceArea y1={cutPct} y2={pctTop} fill={C.crit} fillOpacity={0.08} />
              <ReferenceLine y={warnPct} stroke={C.warn} strokeDasharray="3 3" label={{ value: `WARN ${warnPct}%`, fill: C.axis, fontSize: "var(--fs-xs)", position: "insideBottomLeft" }} />
              <ReferenceLine y={cutPct} stroke={C.crit} strokeDasharray="3 3" label={{ value: `CUT ${cutPct}%`, fill: C.axis, fontSize: "var(--fs-xs)", position: "insideTopRight" }} />
              <Tooltip content={<TipBox unit={{ "Load %": "%" }} />} isAnimationActive={false} />
              {/* null = outside the chart: drawn as a gap, not as a 999 % spike */}
              <Line dataKey="pct" name="Load %" stroke={C.s1} strokeWidth={2} dot={false} isAnimationActive={false} connectNulls={false} />
            </LineChart>
          </ResponsiveContainer>
        </ChartCard>

        <ChartCard title="Boom and swing angle" height={190}
                   legend={[{ label: "Boom", color: C.s1 }, { label: "Swing", color: C.s2 }]}>
          <ResponsiveContainer>
            <LineChart data={data} margin={margin}>
              <CartesianGrid stroke={C.grid} vertical={false} />
              {X}
              <YAxis tick={AXIS_TICK} stroke={C.grid} unit="°" width={56} domain={angDom} ticks={ticksFor(angDom, 5)} />
              <Tooltip content={<TipBox unit={{ Boom: "°", Swing: "°" }} />} isAnimationActive={false} />
              <Line dataKey="boom" name="Boom" stroke={C.s1} strokeWidth={2} dot={false} isAnimationActive={false} />
              <Line dataKey="swing" name="Swing" stroke={C.s2} strokeWidth={2} dot={false} isAnimationActive={false} />
            </LineChart>
          </ResponsiveContainer>
        </ChartCard>

        <ChartCard title="Telescope extension and hook drop" height={190}
                   legend={[{ label: "Extension", color: C.s1 }, { label: "Hook drop", color: C.s3 }]}>
          <ResponsiveContainer>
            <LineChart data={data} margin={margin}>
              <CartesianGrid stroke={C.grid} vertical={false} />
              {X}
              <YAxis tick={AXIS_TICK} stroke={C.grid} unit=" mm" width={68} domain={extDom} ticks={ticksFor(extDom, 25)} />
              <Tooltip content={<TipBox unit={{ Extension: " mm", "Hook drop": " mm" }} />} isAnimationActive={false} />
              <Line dataKey="ext" name="Extension" stroke={C.s1} strokeWidth={2} dot={false} isAnimationActive={false} />
              <Line dataKey="hookDrop" name="Hook drop" stroke={C.s3} strokeWidth={2} dot={false} isAnimationActive={false} />
            </LineChart>
          </ResponsiveContainer>
        </ChartCard>

        {/* Acceleration and jerk are different units: two charts, never one with two y-axes.
            Both are shown above the idle level (0 at rest): only shocks stand out. */}
        <ChartCard title="Boom-tip shocks: acceleration, m/s²" height={170}>
          <ResponsiveContainer>
            <LineChart data={data} margin={margin} syncId="live">
              <CartesianGrid stroke={C.grid} vertical={false} />
              {X}
              <YAxis tick={AXIS_TICK} stroke={C.grid} width={48} domain={accDom} ticks={ticksFor(accDom, 0.5)} tickFormatter={v => (+v).toFixed(1)} />
              <Tooltip content={<TipBox unit={{ "Acceleration above idle": " m/s²" }} />} isAnimationActive={false} />
              <Line dataKey="accel" name="Acceleration above idle" stroke={C.s1} strokeWidth={2} dot={false} isAnimationActive={false} />
            </LineChart>
          </ResponsiveContainer>
        </ChartCard>

        <ChartCard title="Boom-tip shocks: jerk, m/s³" height={170}>
          <ResponsiveContainer>
            <LineChart data={data} margin={margin} syncId="live">
              <CartesianGrid stroke={C.grid} vertical={false} />
              {X}
              <YAxis tick={AXIS_TICK} stroke={C.grid} width={48} domain={jerkDom} ticks={ticksFor(jerkDom, 25)} />
              <Tooltip content={<TipBox unit={{ "Jerk above idle": " m/s³" }} />} isAnimationActive={false} />
              <Line dataKey="jerk" name="Jerk above idle" stroke={C.s2} strokeWidth={2} dot={false} isAnimationActive={false} />
            </LineChart>
          </ResponsiveContainer>
        </ChartCard>

        <ChartCard title="Tipping margin" height={170}>
          {hasTip ? (
            <ResponsiveContainer>
              <LineChart data={data} margin={margin}>
                <CartesianGrid stroke={C.grid} vertical={false} />
                {X}
                <YAxis tick={AXIS_TICK} stroke={C.grid} unit="%" width={52} domain={[0, 100]} />
                <ReferenceArea y1={0} y2={tipWarnPct} fill={C.crit} fillOpacity={0.08} />
                <ReferenceLine y={tipWarnPct} stroke={C.crit} strokeDasharray="3 3"
                               label={{ value: `warn < ${tipWarnPct}%`, fill: C.axis, fontSize: "var(--fs-xs)", position: "insideBottomLeft" }} />
                <Tooltip content={<TipBox unit={{ "Tipping margin": "%" }} />} isAnimationActive={false} />
                <Line dataKey="tip" name="Tipping margin" stroke={C.s3} strokeWidth={2} dot={false} isAnimationActive={false} connectNulls={false} />
              </LineChart>
            </ResponsiveContainer>
          ) : (
            <div className="dl-empty small">The FSR pads read nothing: crane not standing on the sensors, or not stuck on yet.</div>
          )}
        </ChartCard>

        <div className="dl-card dl-now">
          <div className="dl-card-head"><span className="dl-card-title">Now</span></div>
          <div className="dl-now-grid">
            <Stat label="Load" value={`${(last.load * 1000).toFixed(0)} g`} />
            <Stat label="Capacity" value={last.cap > 0 ? `${(last.cap * 1000).toFixed(0)} g` : "—"} />
            <Stat label="Load %" value={last.pct !== null ? `${last.pct.toFixed(1)} %` : "outside"} />
            <Stat label="Boom" value={`${last.boom.toFixed(1)}°`} />
            <Stat label="Swing" value={`${last.swing.toFixed(1)}°`} />
            <Stat label="Tipping" value={last.tip !== null ? `${last.tip.toFixed(0)} %` : "—"} />
          </div>
        </div>
      </ChartGrid>
    </>
  );
}

function Stat({ label, value }: { label: string; value: string }) {
  return (
    <div className="dl-stat">
      <span className="dl-stat-label">{label}</span>
      <span className="dl-stat-val">{value}</span>
    </div>
  );
}
