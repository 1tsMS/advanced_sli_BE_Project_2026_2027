import { useEffect, useState, type RefObject } from "react";
import {
  LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer, ReferenceLine, ReferenceArea,
} from "recharts";
import type { LivePoint } from "../../types";
import { C, AXIS_TICK, ChartCard, TipBox } from "./chartKit";

const REFRESH_MS = 250;       // 4 Hz redraw, whatever the telemetry rate
const MAX_POINTS = 400;       // per chart (60 s at 50 Hz = 3000 raw)

/** Every k-th sample, plus the newest, with t relative to now (negative seconds). */
function snapshot(src: LivePoint[]): LivePoint[] {
  if (!src.length) return [];
  const now = src[src.length - 1].ts;
  const step = Math.max(1, Math.ceil(src.length / MAX_POINTS));
  const out: LivePoint[] = [];
  for (let i = 0; i < src.length; i += step) out.push({ ...src[i], t: +(src[i].ts - now).toFixed(2) });
  if ((src.length - 1) % step) out.push({ ...src[src.length - 1], t: 0 });
  return out;
}

const X = (
  <XAxis dataKey="t" type="number" domain={[-60, 0]} ticks={[-60, -50, -40, -30, -20, -10, 0]}
         tick={AXIS_TICK} stroke={C.grid} tickFormatter={v => `${v}s`} />
);

export function LiveCharts({ history, warnPct = 90, cutPct = 100, tipWarnPct = 30 }: {
  history: RefObject<LivePoint[]>; warnPct?: number; cutPct?: number; tipWarnPct?: number;
}) {
  const [data, setData] = useState<LivePoint[]>([]);
  const [paused, setPaused] = useState(false);

  useEffect(() => {
    if (paused) return;
    const id = setInterval(() => setData(snapshot(history.current ?? [])), REFRESH_MS);
    return () => clearInterval(id);
  }, [history, paused]);

  const last = data[data.length - 1];
  const pctMax = Math.max(120, ...data.map(d => Math.min(d.pct, 300)));
  const hasTip = data.some(d => d.tip !== null);

  const pause = (
    <button className={`dl-icon-btn ${paused ? "on" : ""}`} onClick={() => setPaused(p => !p)}
            title="Freeze the live charts (to read or export them)">
      {paused ? "Resume" : "Pause"}
    </button>
  );

  if (!data.length) {
    return <div className="dl-empty">No telemetry yet. Connect the ESP32 (Settings) and the last 60 s show up here.</div>;
  }

  return (
    <div className="dl-grid">
      <ChartCard title="Load vs rated capacity (live)" height={210} extra={pause}
                 legend={[{ label: "Load", color: C.s1 }, { label: "Rated capacity", color: C.ref, dashed: true }]}>
        <ResponsiveContainer>
          <LineChart data={data} margin={{ top: 6, right: 12, bottom: 0, left: -6 }}>
            <CartesianGrid stroke={C.grid} vertical={false} />
            {X}
            <YAxis tick={AXIS_TICK} stroke={C.grid} unit=" kg" width={58} domain={[0, "auto"]} />
            <Tooltip content={<TipBox unit={{ Load: " kg", "Rated capacity": " kg" }} />} isAnimationActive={false} />
            <Line dataKey="cap" name="Rated capacity" stroke={C.ref} strokeDasharray="5 4" strokeWidth={1.5} dot={false} isAnimationActive={false} />
            <Line dataKey="load" name="Load" stroke={C.s1} strokeWidth={2} dot={false} isAnimationActive={false} />
          </LineChart>
        </ResponsiveContainer>
      </ChartCard>

      <ChartCard title="Load % of capacity (live)" height={210}>
        <ResponsiveContainer>
          <LineChart data={data} margin={{ top: 6, right: 12, bottom: 0, left: -6 }}>
            <CartesianGrid stroke={C.grid} vertical={false} />
            {X}
            <YAxis tick={AXIS_TICK} stroke={C.grid} unit="%" width={52} domain={[0, Math.ceil(pctMax / 20) * 20]} />
            <ReferenceArea y1={warnPct} y2={cutPct} fill={C.warn} fillOpacity={0.08} />
            <ReferenceArea y1={cutPct} y2={Math.ceil(pctMax / 20) * 20} fill={C.crit} fillOpacity={0.08} />
            <ReferenceLine y={warnPct} stroke={C.warn} strokeDasharray="3 3" label={{ value: `WARN ${warnPct}%`, fill: C.axis, fontSize: 10, position: "insideBottomLeft" }} />
            <ReferenceLine y={cutPct} stroke={C.crit} strokeDasharray="3 3" label={{ value: `CUT ${cutPct}%`, fill: C.axis, fontSize: 10, position: "insideTopLeft" }} />
            <Tooltip content={<TipBox unit={{ "Load %": "%" }} />} isAnimationActive={false} />
            <Line dataKey="pct" name="Load %" stroke={C.s1} strokeWidth={2} dot={false} isAnimationActive={false} />
          </LineChart>
        </ResponsiveContainer>
      </ChartCard>

      <ChartCard title="Boom and swing angle (live)" height={190}
                 legend={[{ label: "Boom", color: C.s1 }, { label: "Swing", color: C.s2 }]}>
        <ResponsiveContainer>
          <LineChart data={data} margin={{ top: 6, right: 12, bottom: 0, left: -6 }}>
            <CartesianGrid stroke={C.grid} vertical={false} />
            {X}
            <YAxis tick={AXIS_TICK} stroke={C.grid} unit="°" width={52} />
            <Tooltip content={<TipBox unit={{ Boom: "°", Swing: "°" }} />} isAnimationActive={false} />
            <Line dataKey="boom" name="Boom" stroke={C.s1} strokeWidth={2} dot={false} isAnimationActive={false} />
            <Line dataKey="swing" name="Swing" stroke={C.s2} strokeWidth={2} dot={false} isAnimationActive={false} />
          </LineChart>
        </ResponsiveContainer>
      </ChartCard>

      <ChartCard title="Telescope extension and hook drop (live)" height={190}
                 legend={[{ label: "Extension", color: C.s1 }, { label: "Hook drop", color: C.s3 }]}>
        <ResponsiveContainer>
          <LineChart data={data} margin={{ top: 6, right: 12, bottom: 0, left: -6 }}>
            <CartesianGrid stroke={C.grid} vertical={false} />
            {X}
            <YAxis tick={AXIS_TICK} stroke={C.grid} unit=" mm" width={60} />
            <Tooltip content={<TipBox unit={{ Extension: " mm", "Hook drop": " mm" }} />} isAnimationActive={false} />
            <Line dataKey="ext" name="Extension" stroke={C.s1} strokeWidth={2} dot={false} isAnimationActive={false} />
            <Line dataKey="hookDrop" name="Hook drop" stroke={C.s3} strokeWidth={2} dot={false} isAnimationActive={false} />
          </LineChart>
        </ResponsiveContainer>
      </ChartCard>

      <ChartCard title="Tipping margin (live)" height={170}>
        {hasTip ? (
          <ResponsiveContainer>
            <LineChart data={data} margin={{ top: 6, right: 12, bottom: 0, left: -6 }}>
              <CartesianGrid stroke={C.grid} vertical={false} />
              {X}
              <YAxis tick={AXIS_TICK} stroke={C.grid} unit="%" width={52} domain={[0, 100]} />
              <ReferenceArea y1={0} y2={tipWarnPct} fill={C.crit} fillOpacity={0.08} />
              <ReferenceLine y={tipWarnPct} stroke={C.crit} strokeDasharray="3 3"
                             label={{ value: `warn < ${tipWarnPct}%`, fill: C.axis, fontSize: 10, position: "insideBottomLeft" }} />
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
          <Stat label="Load %" value={last.cap > 0 ? `${last.pct.toFixed(1)} %` : "—"} />
          <Stat label="Boom" value={`${last.boom.toFixed(1)}°`} />
          <Stat label="Swing" value={`${last.swing.toFixed(1)}°`} />
          <Stat label="Tipping" value={last.tip !== null ? `${last.tip.toFixed(0)} %` : "—"} />
        </div>
      </div>
    </div>
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
