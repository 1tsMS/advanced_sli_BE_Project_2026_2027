import type { HistoryPoint } from "../../types";

export interface ChartSeries {
  label: string;
  color: string;
  pick: (p: HistoryPoint) => number;
  dashed?: boolean;
}

interface LineChartProps {
  title: string;
  unit: string;
  points: HistoryPoint[];
  series: ChartSeries[];
  /** Length of the visible time window in seconds */
  windowSeconds: number;
}

const W = 600;
const H = 150;
const PAD = { left: 44, right: 10, top: 8, bottom: 20 };

/** Nice-looking axis bounds around the data. */
function bounds(values: number[]): [number, number] {
  let lo = Math.min(...values);
  let hi = Math.max(...values);
  if (!Number.isFinite(lo) || !Number.isFinite(hi)) return [0, 1];
  if (hi - lo < 1e-6) { lo -= 0.5; hi += 0.5; }
  const pad = (hi - lo) * 0.1;
  return [lo - pad, hi + pad];
}

/**
 * Small dependency-free SVG line chart for a rolling time window.
 * The x axis always spans the last `windowSeconds`, ending at the newest sample.
 */
export function LineChart({ title, unit, points, series, windowSeconds }: LineChartProps) {
  const tEnd = points.length ? points[points.length - 1].t : 0;
  const tStart = tEnd - windowSeconds;

  const all = points.flatMap(p => series.map(s => s.pick(p))).filter(Number.isFinite);
  const [yMin, yMax] = all.length ? bounds(all) : [0, 1];

  const x = (t: number) => PAD.left + ((t - tStart) / windowSeconds) * (W - PAD.left - PAD.right);
  const y = (v: number) => PAD.top + (1 - (v - yMin) / (yMax - yMin)) * (H - PAD.top - PAD.bottom);

  const yTicks = [0, 0.25, 0.5, 0.75, 1].map(f => yMin + f * (yMax - yMin));
  const xTicks = [0, 10, 20, 30].filter(s => s <= windowSeconds);

  return (
    <div style={{ marginBottom: 6 }}>
      <div style={{ display: "flex", alignItems: "center", gap: 12, padding: "0 4px 4px" }}>
        <span className="section-label" style={{ margin: 0 }}>{title}</span>
        <span className="dim-text" style={{ fontSize: 10 }}>{unit}</span>
        <span style={{ flex: 1 }} />
        {series.map(s => (
          <span key={s.label} style={{ fontSize: 10.5, color: s.color, fontFamily: "var(--font-mono)" }}>
            ● {s.label}{points.length ? ` ${s.pick(points[points.length - 1]).toFixed(1)}` : ""}
          </span>
        ))}
      </div>

      <svg viewBox={`0 0 ${W} ${H}`} style={{ width: "100%", display: "block" }} role="img" aria-label={title}>
        {/* Grid + y labels */}
        {yTicks.map((v, i) => (
          <g key={i}>
            <line x1={PAD.left} x2={W - PAD.right} y1={y(v)} y2={y(v)} stroke="rgba(255,255,255,0.06)" />
            <text x={PAD.left - 5} y={y(v) + 3} textAnchor="end" fontSize="9" fill="var(--text-muted)"
                  fontFamily="var(--font-mono)">
              {v.toFixed(Math.abs(yMax - yMin) < 10 ? 1 : 0)}
            </text>
          </g>
        ))}
        {/* x labels */}
        {xTicks.map(s => (
          <text key={s} x={x(tEnd - s)} y={H - 5} textAnchor="middle" fontSize="9" fill="var(--text-muted)"
                fontFamily="var(--font-mono)">
            {s === 0 ? "now" : `-${s}s`}
          </text>
        ))}

        {points.length < 2 ? (
          <text x={W / 2} y={H / 2} textAnchor="middle" fontSize="11" fill="var(--text-muted)">
            Waiting for telemetry…
          </text>
        ) : (
          series.map(s => (
            <polyline
              key={s.label}
              fill="none"
              stroke={s.color}
              strokeWidth="1.5"
              strokeDasharray={s.dashed ? "4 3" : undefined}
              strokeLinejoin="round"
              points={points.map(p => `${x(p.t).toFixed(1)},${y(s.pick(p)).toFixed(1)}`).join(" ")}
            />
          ))
        )}
      </svg>
    </div>
  );
}
