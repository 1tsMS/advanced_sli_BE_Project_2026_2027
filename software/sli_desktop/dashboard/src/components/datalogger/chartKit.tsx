import { useRef, type ReactNode } from "react";
import { Download } from "lucide-react";

/**
 * Chart colours. Series use the validated categorical order (dark-surface steps);
 * WARN / CUT use the reserved status colours and never mean "a series".
 * EXPORT_LIGHT maps every dark-surface colour to its light-surface step, so a
 * PNG for the report / paper comes out dark-on-white.
 */
export const C = {
  s1: "#3987e5",       // blue   - primary series (load)
  s2: "#d95926",       // orange - second series
  s3: "#199e70",       // aqua   - third series
  ref: "#8B95AD",      // reference lines (capacity) - neutral, dashed
  warn: "#fab219",
  crit: "#d03b3b",
  grid: "#232a3a",
  axis: "#8B95AD",
  surface: "#121620",
};

const EXPORT_LIGHT: Record<string, string> = {
  "#3987e5": "#2a78d6", "#d95926": "#eb6834", "#199e70": "#1baf7a",
  "#8B95AD": "#52514e", "#8b95ad": "#52514e", "#232a3a": "#e3e3df", "#121620": "#ffffff",
  "#E2E6F0": "#0b0b0b", "#e2e6f0": "#0b0b0b",
};

/** Phase timeline colours (always shown with a text label next to them). */
export const PHASE_COLOR: Record<string, string> = {
  idle: "#3a4255", approach: "#5a6478", "pick-up": "#3987e5", hoist: "#199e70",
  slew: "#9085e9", hold: "#c98500", "set-down": "#d55181",
};

export const AXIS_TICK = { fill: C.axis, fontSize: 10, fontFamily: "JetBrains Mono, monospace" };

export function fmtS(s: number): string {
  const m = Math.floor(s / 60);
  return m ? `${m}:${String(Math.floor(s % 60)).padStart(2, "0")}` : `${s.toFixed(1)} s`;
}

function download(name: string, blob: Blob) {
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}

/** The chart's SVG, light-themed for print, with its title on top. */
function svgForExport(root: HTMLElement, title: string): { text: string; w: number; h: number } | null {
  const svg = root.querySelector("svg.recharts-surface") as SVGSVGElement | null;
  if (!svg) return null;
  const w = svg.clientWidth || 800, h = svg.clientHeight || 300;
  const clone = svg.cloneNode(true) as SVGSVGElement;
  clone.setAttribute("xmlns", "http://www.w3.org/2000/svg");
  clone.setAttribute("width", String(w));
  clone.setAttribute("height", String(h + 28));
  clone.setAttribute("viewBox", `0 -28 ${w} ${h + 28}`);
  let text = new XMLSerializer().serializeToString(clone);
  for (const [dark, light] of Object.entries(EXPORT_LIGHT)) text = text.split(dark).join(light);
  const head = `<rect x="0" y="-28" width="${w}" height="${h + 28}" fill="#ffffff"/>` +
    `<text x="8" y="-9" font-family="Inter, Arial, sans-serif" font-size="13" font-weight="600" fill="#0b0b0b">${title}</text>`;
  text = text.replace(/(<svg[^>]*>)/, `$1${head}`);
  return { text, w, h: h + 28 };
}

function exportChart(root: HTMLElement | null, title: string, kind: "png" | "svg") {
  if (!root) return;
  const out = svgForExport(root, title);
  if (!out) return;
  const base = title.toLowerCase().replace(/[^a-z0-9]+/g, "_");
  if (kind === "svg") { download(`${base}.svg`, new Blob([out.text], { type: "image/svg+xml" })); return; }
  const img = new Image();
  const scale = 3;                                        // print resolution
  img.onload = () => {
    const c = document.createElement("canvas");
    c.width = out.w * scale; c.height = out.h * scale;
    const ctx = c.getContext("2d")!;
    ctx.scale(scale, scale);
    ctx.drawImage(img, 0, 0);
    c.toBlob(b => b && download(`${base}.png`, b), "image/png");
  };
  img.src = "data:image/svg+xml;charset=utf-8," + encodeURIComponent(out.text);
}

export interface LegendItem { label: string; color: string; dashed?: boolean; band?: boolean; }

/** Panel around one chart: title, legend, PNG / SVG export. */
export function ChartCard({ title, legend, height = 200, exportable = true, children, extra }: {
  title: string; legend?: LegendItem[]; height?: number; exportable?: boolean;
  children: ReactNode; extra?: ReactNode;
}) {
  const ref = useRef<HTMLDivElement>(null);
  return (
    <div className="dl-card">
      <div className="dl-card-head">
        <span className="dl-card-title">{title}</span>
        {legend && legend.length > 1 && (
          <span className="dl-legend">
            {legend.map(l => (
              <span key={l.label} className="dl-legend-item">
                <span className={`dl-swatch ${l.dashed ? "dashed" : ""} ${l.band ? "band" : ""}`}
                      style={{ background: l.band || !l.dashed ? l.color : undefined, borderColor: l.color }} />
                {l.label}
              </span>
            ))}
          </span>
        )}
        <span className="dl-card-fill" />
        {extra}
        {exportable && (
          <>
            <button className="dl-icon-btn" title="Download PNG (white background, 3x)" onClick={() => exportChart(ref.current, title, "png")}>
              <Download size={11} /> PNG
            </button>
            <button className="dl-icon-btn" title="Download SVG (vector, for the paper)" onClick={() => exportChart(ref.current, title, "svg")}>
              SVG
            </button>
          </>
        )}
      </div>
      <div ref={ref} style={{ height }}>{children}</div>
    </div>
  );
}

/** Dark tooltip box shared by all charts. */
export function TipBox({ active, payload, label, unit }: {
  active?: boolean; payload?: { name: string; value: number | null; color: string }[];
  label?: number; unit?: Record<string, string>;
}) {
  if (!active || !payload?.length) return null;
  return (
    <div className="dl-tip">
      <div className="dl-tip-t">t = {typeof label === "number" ? label.toFixed(2) : label} s</div>
      {payload.filter(p => p.value !== null && p.value !== undefined).map(p => (
        <div key={p.name} className="dl-tip-row">
          <span className="dl-swatch" style={{ background: p.color }} />
          <span>{p.name}</span>
          <b>{typeof p.value === "number" ? p.value.toFixed(p.value < 10 ? 3 : 1) : p.value}{unit?.[p.name] ?? ""}</b>
        </div>
      ))}
    </div>
  );
}
