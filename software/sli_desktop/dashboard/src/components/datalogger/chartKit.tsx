import { createContext, useContext, useRef, type CSSProperties, type ReactNode } from "react";
import { Download } from "lucide-react";

/**
 * Chart colours = theme tokens (index.css), so every chart follows the theme.
 * Series use the validated categorical order (--c1..--c5); WARN / CUT use the
 * reserved status colours and never mean "a series". Exports always use the
 * print (white) values in PRINT, whatever the screen theme.
 */
export const C = {
  s1: "var(--c1)",     // blue   - primary series (load)
  s2: "var(--c2)",     // orange - second series
  s3: "var(--c3)",     // aqua   - third series
  s4: "var(--c4)",     // violet
  s5: "var(--c5)",     // magenta
  ref: "var(--chart-ref)",   // reference lines (capacity) - neutral, dashed
  warn: "var(--warn)",
  crit: "var(--crit)",
  grid: "var(--chart-grid)",
  axis: "var(--chart-axis)",
  surface: "var(--surface)",
};

/** Token values for exported charts (white background, like the projector theme). */
const PRINT: Record<string, string> = {
  "--c1": "#2A6FD0", "--c2": "#C9521C", "--c3": "#0F8A68", "--c4": "#6250D6", "--c5": "#C4407A",
  "--chart-ref": "#33383E", "--chart-grid": "#D9DCE0", "--chart-axis": "#1F2328",
  "--warn": "#8A4F00", "--crit": "#B00000", "--surface": "#FFFFFF", "--surface-3": "#E6E8EB",
  "--line-strong": "#2A2E33", "--text": "#000000", "--text-2": "#22262B", "--text-3": "#444A52",
  "--fs-xs": "12px", "--fs-sm": "13px",
};

/** Phase timeline colours (always shown with a text label next to them). */
export const PHASE_COLOR: Record<string, string> = {
  idle: "var(--surface-3)", approach: "var(--line-strong)", "pick-up": "var(--c1)", hoist: "var(--c3)",
  slew: "var(--c4)", hold: "var(--c2)", "set-down": "var(--c5)",
};

export const AXIS_TICK = { fill: C.axis, fontSize: "var(--fs-xs)", fontFamily: "JetBrains Mono, monospace" };

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
  // A standalone SVG has no stylesheet: swap every var(--token) for its print value
  text = text.replace(/var\((--[\w-]+)\)/g, (_, name: string) => PRINT[name] ?? "#000000");
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

/**
 * Chart layout presets (Data Logger toolbar):
 *   default   medium charts, 2 per row, the page scrolls
 *   overview  every chart on one screen, no scrolling, charts shrink to fit
 *   detail    one chart per row, tall, for reading values off it
 */
export type ChartLayout = "default" | "overview" | "detail";
export const CHART_LAYOUTS: { id: ChartLayout; label: string; hint: string }[] = [
  { id: "default",  label: "Default",  hint: "Medium charts, 2 per row" },
  { id: "overview", label: "Overview", hint: "Every chart on one screen" },
  { id: "detail",   label: "Detail",   hint: "One big chart per row" },
];
export const ChartLayoutContext = createContext<ChartLayout>("default");

/** The grid every chart card sits in; its columns and heights follow the layout preset. */
export function ChartGrid({ children }: { children: ReactNode }) {
  const layout = useContext(ChartLayoutContext);
  return <div className={`dl-grid layout-${layout}`}>{children}</div>;
}

export interface LegendItem { label: string; color: string; dashed?: boolean; band?: boolean; }

/** Panel around one chart: title, legend, PNG / SVG export. */
export function ChartCard({ title, legend, height = 200, exportable = true, children, extra, className = "" }: {
  title: string; legend?: LegendItem[]; height?: number; exportable?: boolean;
  children: ReactNode; extra?: ReactNode; className?: string;
}) {
  const ref = useRef<HTMLDivElement>(null);
  return (
    <div className={`dl-card ${className}`}>
      <div className="dl-card-head">
        <span className="dl-card-title" title={title}>{title}</span>
        {extra}
        {exportable && (
          <span className="dl-export">
            <button className="dl-icon-btn" title="Download PNG (white background, 3x)" onClick={() => exportChart(ref.current, title, "png")}>
              <Download size={13} /> PNG
            </button>
            <button className="dl-icon-btn" title="Download SVG (vector, for the paper)" onClick={() => exportChart(ref.current, title, "svg")}>
              SVG
            </button>
          </span>
        )}
      </div>
      {legend && legend.length > 1 && (
        <div className="dl-legend">
          {legend.map(l => (
            <span key={l.label} className="dl-legend-item">
              <span className={`dl-swatch ${l.dashed ? "dashed" : ""} ${l.band ? "band" : ""}`}
                    style={{ background: l.band || !l.dashed ? l.color : undefined, borderColor: l.color }} />
              {l.label}
            </span>
          ))}
        </div>
      )}
      <div ref={ref} className="dl-plot" style={{ "--h": `${height}px` } as CSSProperties}>{children}</div>
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
