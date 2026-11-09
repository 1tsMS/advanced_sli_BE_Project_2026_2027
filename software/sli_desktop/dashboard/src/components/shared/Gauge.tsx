/**
 * 270° dial in SVG, drawn with the theme tokens. ISA-101 style: the value arc
 * is neutral while normal and turns WARN / CRIT only past its thresholds; the
 * WARN / CRIT ranges are marked on the track so the value is read in context.
 * Bidirectional dials (min < 0, e.g. tilt) grow from the centre and use |value|.
 */
interface GaugeProps {
  value: number;
  min?: number;
  max: number;
  label: string;
  unit?: string;
  warnAt?: number;          // |value| at or above = WARN
  critAt?: number;          // |value| at or above = CRIT
  size?: number;
  /** Shown instead of the number (e.g. ["OUT", "CHART"]) when the value has no meaning. */
  text?: [string, string] | null;
}

const SWEEP = 270;
const START = 135;          // degrees, clockwise from +x: bottom-left

function polar(cx: number, cy: number, r: number, deg: number) {
  const a = (deg * Math.PI) / 180;
  return [cx + r * Math.cos(a), cy + r * Math.sin(a)];
}

function arc(cx: number, cy: number, r: number, from: number, to: number) {
  if (to - from < 0.01) return "";
  const [x1, y1] = polar(cx, cy, r, from);
  const [x2, y2] = polar(cx, cy, r, to);
  return `M ${x1} ${y1} A ${r} ${r} 0 ${to - from > 180 ? 1 : 0} 1 ${x2} ${y2}`;
}

export function Gauge({ value, min = 0, max, label, unit = "", warnAt, critAt, size = 110, text = null }: GaugeProps) {
  const cx = size / 2, cy = size / 2, r = size * 0.38, w = size * 0.09;
  const bi = min < 0;
  const span = bi ? Math.max(-min, max) : max - min;
  const frac = (v: number) => Math.max(0, Math.min(1, v / (span || 1)));
  const ang = (f: number) => START + SWEEP * f;

  const mag = bi ? Math.abs(value) : value - min;
  const judged = bi ? Math.abs(value) : value;
  const level = !Number.isFinite(value) || text ? "" : critAt !== undefined && judged >= critAt ? "crit"
    : warnAt !== undefined && judged >= warnAt ? "warn" : "";
  const stroke = level === "crit" ? "var(--crit)" : level === "warn" ? "var(--warn)" : "var(--text-2)";

  // value arc
  let from = START, to = START;
  if (Number.isFinite(value) && !text) {
    if (bi) {
      const mid = START + SWEEP / 2, d = (SWEEP / 2) * frac(Math.abs(value));
      [from, to] = value < 0 ? [mid - d, mid] : [mid, mid + d];
    } else {
      to = ang(frac(mag));
    }
  }

  // threshold bands on the track
  const bands: { from: number; to: number; cls: string }[] = [];
  const band = (lo: number, hi: number, cls: string) => {
    if (bi) {
      const mid = START + SWEEP / 2;
      const a = (SWEEP / 2) * frac(lo), b = (SWEEP / 2) * frac(hi);
      bands.push({ from: mid + a, to: mid + b, cls }, { from: mid - b, to: mid - a, cls });
    } else {
      bands.push({ from: ang(frac(lo - min)), to: ang(frac(hi - min)), cls });
    }
  };
  if (warnAt !== undefined) band(warnAt, critAt ?? span, "warn");
  if (critAt !== undefined) band(critAt, bi ? span : max, "crit");

  const shown = text ? text[0] : Number.isFinite(value) ? (Math.abs(value) >= 100 ? value.toFixed(0) : value.toFixed(1)) : "–";

  return (
    <svg className="gauge" width={size} height={size} viewBox={`0 0 ${size} ${size}`} role="meter"
         aria-label={`${label} ${shown} ${unit}`} aria-valuenow={value} aria-valuemin={min} aria-valuemax={max}>
      <path d={arc(cx, cy, r, START, START + SWEEP)} stroke="var(--surface-3)" strokeWidth={w} fill="none" strokeLinecap="round" />
      {bands.map((b, i) => (
        <path key={i} d={arc(cx, cy, r + w * 0.85, b.from, b.to)} fill="none" strokeWidth={w * 0.35}
              stroke={b.cls === "crit" ? "var(--crit)" : "var(--warn)"} opacity={0.7} />
      ))}
      {bi && (() => {
        const [x1, y1] = polar(cx, cy, r - w, START + SWEEP / 2);
        const [x2, y2] = polar(cx, cy, r + w, START + SWEEP / 2);
        return <line x1={x1} y1={y1} x2={x2} y2={y2} stroke="var(--line-strong)" strokeWidth={1.5} />;
      })()}
      <path d={arc(cx, cy, r, from, to)} stroke={stroke} strokeWidth={w} fill="none" strokeLinecap="round" />
      <text x={cx} y={cy - size * 0.02} textAnchor="middle" dominantBaseline="middle"
            fill={level ? stroke : "var(--text)"} fontFamily="var(--font-mono)" fontWeight={800} fontSize={size * 0.19}>
        {level === "crit" ? "▲" : ""}{shown}
      </text>
      <text x={cx} y={cy + size * 0.15} textAnchor="middle" fill="var(--text-3)" fontFamily="var(--font-sans)" fontSize={size * 0.1}>
        {text ? text[1] : unit}
      </text>
      <text x={cx} y={cy + size * 0.4} textAnchor="middle" fill="var(--text-2)" fontFamily="var(--font-sans)" fontWeight={700} fontSize={size * 0.1}>
        {label.toUpperCase()}
      </text>
    </svg>
  );
}
