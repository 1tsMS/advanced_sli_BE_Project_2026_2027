import { useEffect, useMemo, useRef, useState } from "react";
import type { LoadChartProfile, SliState } from "../../types";
import { capacityAt, rampColor, zoneFactor, CAP_RAMP } from "./chartMath";

export interface LivePose {
  boomDeg: number;
  extMm: number;
  swingDeg: number;
  loadKg: number;
}

interface RangeDiagramProps {
  profile: LoadChartProfile;
  boomBaseMm: number;          // boom length at 0 mm extension (pivot -> tip): drawing only
  live: LivePose | null;
  liveState: SliState | null;
}

const PAD = { l: 44, r: 16, t: 14, b: 30 };   // px around the plot (axis labels)
const SUB = 4;                                // sub-sectors per chart cell for a smooth field
const RAD = Math.PI / 180;

const STATE_COLOR: Record<SliState, string> = {
  OK: "var(--green)", WARN: "var(--amber)", CUT: "var(--red)", FAULT: "var(--red)", ESTOP: "var(--red)",
};

/** "Nice" grid step for a span in mm. */
function niceStep(span: number): number {
  const raw = span / 5;
  const p = Math.pow(10, Math.floor(Math.log10(raw)));
  const m = raw / p;
  return (m < 1.5 ? 1 : m < 3.5 ? 2 : m < 7.5 ? 5 : 10) * p;
}

/**
 * Range diagram like a real crane chart: the boom drawn from its pivot in true
 * geometry (reach x height, mm), capacity as a sequential heat field over the
 * angle x extension envelope, the live boom and hook point on top. Blank =
 * outside the chart, where the ESP32 cuts motion.
 */
export function RangeDiagram({ profile, boomBaseMm, live, liveState }: RangeDiagramProps) {
  const wrapRef = useRef<HTMLDivElement>(null);
  const [size, setSize] = useState({ w: 600, h: 360 });
  const [hover, setHover] = useState<{ x: number; y: number; deg: number; ext: number; cap: number } | null>(null);
  // Fit: crop to the capacity envelope (a short telescope makes it a thin band far from the pivot)
  const [fit, setFit] = useState(true);

  useEffect(() => {
    const el = wrapRef.current;
    if (!el) return;
    const ro = new ResizeObserver(([e]) => {
      const { width, height } = e.contentRect;
      if (width > 0 && height > 0) setSize({ w: width, h: height });
    });
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  const A = profile.anglesDeg, E = profile.extensionsMM;
  const len = (ext: number) => boomBaseMm + ext;
  const swing = live?.swingDeg ?? 0;
  const [zFactor, zIdx] = zoneFactor(profile, swing);

  // ---- Geometry: bounding box of the envelope (+ the pivot unless fitting) ----
  const geo = useMemo(() => {
    const pts: [number, number][] = fit ? [] : [[0, 0]];
    for (const a of [A[0], A[A.length - 1], ...(A[0] < 0 && A[A.length - 1] > 0 ? [0] : [])]) {
      for (const e of [E[0], E[E.length - 1]]) pts.push([len(e) * Math.cos(a * RAD), len(e) * Math.sin(a * RAD)]);
    }
    const xs = pts.map(p => p[0]), ys = pts.map(p => p[1]);
    const spanX = Math.max(...xs) - Math.min(...xs), spanY = Math.max(...ys) - Math.min(...ys);
    const m = fit ? 0.12 * Math.max(spanX, spanY) : 0;            // room for the ray labels
    const x0 = Math.min(...xs) - m, x1 = Math.max(...xs) + m + (fit ? 0 : 0.04 * spanX);
    const y0 = Math.min(...ys) - m, y1 = Math.max(...ys) + m + (fit ? 0 : 0.08 * spanY) + 1;
    const pw = Math.max(10, size.w - PAD.l - PAD.r), ph = Math.max(10, size.h - PAD.t - PAD.b);
    const k = Math.min(pw / (x1 - x0 || 1), ph / (y1 - y0 || 1));      // px per mm, same on both axes
    const ox = PAD.l + (pw - (x1 - x0) * k) / 2 - x0 * k;
    const oy = PAD.t + (ph - (y1 - y0) * k) / 2 + y1 * k;
    return { x0, x1, y0, y1, k, ox, oy };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [A.join(), E.join(), boomBaseMm, size.w, size.h, fit]);

  const px = (r: number, deg: number): [number, number] =>
    [geo.ox + r * Math.cos(deg * RAD) * geo.k, geo.oy - r * Math.sin(deg * RAD) * geo.k];

  const sector = (r0: number, r1: number, a0: number, a1: number) => {
    const [x00, y00] = px(r0, a0), [x10, y10] = px(r1, a0), [x11, y11] = px(r1, a1), [x01, y01] = px(r0, a1);
    const R0 = r0 * geo.k, R1 = r1 * geo.k;
    return `M${x00},${y00}L${x10},${y10}A${R1},${R1} 0 0 0 ${x11},${y11}L${x01},${y01}A${R0},${R0} 0 0 1 ${x00},${y00}Z`;
  };

  // ---- Heat field (memoised: telemetry re-renders 50x/s) ----
  const { field, outlines, capMin, capMax } = useMemo(() => {
    const all = profile.capacityKg.flat().map(v => v * zFactor).filter(Number.isFinite);
    const lo = Math.min(...all), hi = Math.max(...all);
    const f: { d: string; c: string }[] = [];
    const o: string[] = [];
    for (let i = 0; i + 1 < A.length; i++) {
      for (let j = 0; j + 1 < E.length; j++) {
        for (let si = 0; si < SUB; si++) {
          for (let sj = 0; sj < SUB; sj++) {
            const a0 = A[i] + (A[i + 1] - A[i]) * si / SUB, a1 = A[i] + (A[i + 1] - A[i]) * (si + 1) / SUB;
            const e0 = E[j] + (E[j + 1] - E[j]) * sj / SUB, e1 = E[j] + (E[j + 1] - E[j]) * (sj + 1) / SUB;
            const cap = capacityAt(profile, (a0 + a1) / 2, (e0 + e1) / 2, swing);
            f.push({ d: sector(len(e0), len(e1), a0, a1), c: rampColor(cap, lo, hi) });
          }
        }
        o.push(sector(len(E[j]), len(E[j + 1]), A[i], A[i + 1]));
      }
    }
    return { field: f, outlines: o, capMin: lo, capMax: hi };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [profile, zFactor, geo, swing]);

  // ---- Axis ticks (mm) ----
  const step = niceStep(Math.max(geo.x1 - geo.x0, geo.y1 - geo.y0));
  const ticks = (from: number, to: number) => {
    const out: number[] = [];
    for (let v = Math.ceil(from / step) * step; v <= to + 1e-6; v += step) out.push(Math.round(v));
    return out;
  };
  const xTicks = ticks(fit ? geo.x0 : Math.max(0, geo.x0), geo.x1), yTicks = ticks(geo.y0, geo.y1);

  const onMove = (e: React.MouseEvent<SVGSVGElement>) => {
    const r = e.currentTarget.getBoundingClientRect();
    const mx = e.clientX - r.left, my = e.clientY - r.top;
    const x = (mx - geo.ox) / geo.k, y = (geo.oy - my) / geo.k;
    const deg = Math.atan2(y, x) / RAD, ext = Math.hypot(x, y) - boomBaseMm;
    const cap = capacityAt(profile, deg, ext, swing);
    setHover(cap > 0 ? { x: mx, y: my, deg, ext, cap } : null);
  };

  // ---- Live boom ----
  const liveTip = live && Number.isFinite(live.boomDeg) && Number.isFinite(live.extMm)
    ? px(len(live.extMm), live.boomDeg) : null;
  const pivot = px(0, 0);
  const liveColor = liveState ? STATE_COLOR[liveState] : "var(--text-secondary)";

  return (
    <div className="lc-diagram">
      <div className="lc-diagram-plot" ref={wrapRef}>
        <svg width={size.w} height={size.h} onMouseMove={onMove} onMouseLeave={() => setHover(null)}
             role="img" aria-label={`Range diagram for ${profile.name}`}>
          {/* recessive grid */}
          {xTicks.map(v => (
            <g key={`x${v}`}>
              <line x1={geo.ox + v * geo.k} x2={geo.ox + v * geo.k} y1={PAD.t} y2={size.h - PAD.b} className="lc-grid" />
              <text x={geo.ox + v * geo.k} y={size.h - PAD.b + 14} className="lc-axis-t" textAnchor="middle">{v}</text>
            </g>
          ))}
          {yTicks.map(v => (
            <g key={`y${v}`}>
              <line x1={PAD.l} x2={size.w - PAD.r} y1={geo.oy - v * geo.k} y2={geo.oy - v * geo.k} className="lc-grid" />
              <text x={PAD.l - 6} y={geo.oy - v * geo.k + 3} className="lc-axis-t" textAnchor="end">{v}</text>
            </g>
          ))}
          <text x={size.w - PAD.r} y={size.h - 4} className="lc-axis-t" textAnchor="end">reach, mm</text>
          <text x={4} y={PAD.t + 2} className="lc-axis-t">height</text>

          {/* capacity field + 2 px surface gaps between chart cells */}
          {field.map((s, i) => <path key={i} d={s.d} fill={s.c} />)}
          {outlines.map((d, i) => <path key={`o${i}`} d={d} fill="none" className="lc-cell-gap" />)}

          {/* angle rays + labels */}
          {A.map(a => {
            const [x1, y1] = px(len(E[E.length - 1]) + 6 / geo.k, a);
            const [lx, ly] = px(len(E[E.length - 1]) + 16 / geo.k, a);
            const [x0, y0] = px(len(E[0]), a);
            return (
              <g key={`a${a}`}>
                <line x1={x0} y1={y0} x2={x1} y2={y1} className="lc-ray" />
                <text x={lx} y={ly + 3} className="lc-ray-t" textAnchor="start">{a}°</text>
              </g>
            );
          })}
          {/* innermost + outermost extension labelled under the lowest ray (all of them collide) */}
          {[E[0], E[E.length - 1]].map((e, i) => {
            const [lx, ly] = px(len(e), A[0]);
            return (
              <text key={`e${i}`} x={lx} y={ly + 13} className="lc-ext-t" textAnchor={i === 0 ? "end" : "start"}>
                {e} mm
              </text>
            );
          })}

          {/* pivot */}
          <rect x={pivot[0] - 4} y={pivot[1] - 4} width={8} height={8} className="lc-pivot" />

          {/* live boom + hook point */}
          {liveTip && (
            <g>
              <line x1={pivot[0]} y1={pivot[1]} x2={liveTip[0]} y2={liveTip[1]} className="lc-boom" />
              <circle cx={liveTip[0]} cy={liveTip[1]} r={6} fill={liveColor} className="lc-tip" />
            </g>
          )}
        </svg>

        {hover && (
          <div className="lc-tooltip" style={{
            left: Math.min(hover.x + 14, size.w - 170), top: Math.max(4, hover.y - 54),
          }}>
            <div className="lc-tt-k">{hover.deg.toFixed(1)}° · {hover.ext.toFixed(1)} mm</div>
            <div className="lc-tt-v">{hover.cap.toFixed(3)} kg rated</div>
            {live && live.loadKg > 0.005 && (
              <div className="lc-tt-k">
                {live.loadKg.toFixed(3)} kg here = {(live.loadKg / hover.cap * 100).toFixed(0)} %
              </div>
            )}
          </div>
        )}
        {!liveTip && <div className="lc-nolive">No telemetry: live boom hidden</div>}
      </div>

      <div className="lc-legend">
        <span className="lc-legend-k">Rated capacity{zIdx >= 0 ? ` (zone ${zIdx + 1} ×${zFactor})` : ""}</span>
        <span className="lc-legend-v">{Number.isFinite(capMin) ? capMin.toFixed(2) : "–"} kg</span>
        <span className="lc-legend-bar">
          {CAP_RAMP.map(c => <span key={c} style={{ background: c }} />)}
        </span>
        <span className="lc-legend-v">{Number.isFinite(capMax) ? capMax.toFixed(2) : "–"} kg</span>
        <span className="lc-legend-k" style={{ marginLeft: "auto" }}>blank = outside chart (cut)</span>
        <button className="btn-ghost" style={{ padding: "1px 8px", fontSize: 10 }} onClick={() => setFit(f => !f)}
                title={fit ? "Show the whole crane from the pivot" : "Zoom to the chart envelope"}>
          {fit ? "Whole crane" : "Fit chart"}
        </button>
      </div>
    </div>
  );
}
