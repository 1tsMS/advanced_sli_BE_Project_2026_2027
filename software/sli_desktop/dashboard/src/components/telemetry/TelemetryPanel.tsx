import type { TelemetryFrame, SafetyStatus, LoadChartProfile } from "../../types";
import { loadStatus } from "./loadStatus";
import { allowedMotions, reasonText } from "../sli/sliStatus";
import { hookArmMm, TELE_RETRACTED_MM } from "../../geometry";
import { StaleChart } from "./StaleChart";
/** Bar scale: 0-120 % so the CUT band is visible past the limit. */
const BAR_MAX = 120;

const fmt = (v: number | undefined, d = 1) => (v !== undefined && Number.isFinite(v) ? v.toFixed(d) : "–");

type Level = "" | "warn" | "crit";

function Tile({ k, v, unit, level = "", title }: { k: string; v: string; unit: string; level?: Level; title?: string }) {
  return (
    <div className={`tel-tile ${level}`} title={title}>
      <div className="tel-tile-k">{level === "crit" ? "▲ " : level === "warn" ? "△ " : ""}{k}</div>
      <div className="tel-tile-v">{v}<small>{unit}</small></div>
    </div>
  );
}

/**
 * L1 overview of the crane (ISA-101 level 1): the hook load against its rated
 * capacity, then the eight values an operator watches. Fixed layout, never
 * scrolls; everything else lives in Debug & Calibrate / Data Logger.
 */
/** What to do about the pose being outside the chart. */
function outsideFix(f: TelemetryFrame | null, chart: LoadChartProfile | null | undefined): string {
  if (!f || !chart) return "Move back into the chart's angle / extension range.";
  const a = chart.anglesDeg, e = chart.extensionsMM;
  if (a.length && f.boomAngle < a[0] - 1)
    return `Raise the boom above ${a[0]}°. If the boom is already resting on its stop, its IMU zero is off: redo IMU zero in Calibrate at the resting pose.`;
  if (a.length && f.boomAngle > a[a.length - 1] + 1) return `Lower the boom below ${a[a.length - 1]}°.`;
  if (e.length && f.extensionMM < e[0] - 2) {
    // Retracted reads 0: a chart starting higher is an old chart, not a calibration problem
    if (Math.abs(f.extensionMM - TELE_RETRACTED_MM) < 2)
      return `The telescope is fully retracted (${TELE_RETRACTED_MM} mm) but the chart on the crane only starts at ${e[0]} mm: it is an old chart. Upload one that starts at ${TELE_RETRACTED_MM} mm (Load Chart tab).`;
    return `Extend to at least ${e[0]} mm, or upload a chart that covers this extension.`;
  }
  if (e.length && f.extensionMM > e[e.length - 1] + 2) return `Retract below ${e[e.length - 1]} mm.`;
  return "Move back into the chart's angle / extension range.";
}

/** Which chart edge the pose is past, in words (firmware tolerance: 1° / 2 mm). */
function outsideReason(f: TelemetryFrame | null, chart: LoadChartProfile | null | undefined): string {
  if (!f || !chart) return "pose not in the load chart";
  const a = chart.anglesDeg, e = chart.extensionsMM;
  if (e.length && f.extensionMM < e[0] - 2) return `extension ${f.extensionMM.toFixed(0)} mm < chart ${e[0]} mm`;
  if (e.length && f.extensionMM > e[e.length - 1] + 2) return `extension ${f.extensionMM.toFixed(0)} mm > chart ${e[e.length - 1]} mm`;
  if (a.length && f.boomAngle < a[0] - 1) return `boom ${f.boomAngle.toFixed(1)}° < chart ${a[0]}°`;
  if (a.length && f.boomAngle > a[a.length - 1] + 1) return `boom ${f.boomAngle.toFixed(1)}° > chart ${a[a.length - 1]}°`;
  return "pose not in the load chart";
}

export function TelemetryPanel({ frame: f, safety, chart, warnPct = 90, cutPct = 100 }: {
  frame: TelemetryFrame | null; safety: SafetyStatus | null; chart?: LoadChartProfile | null;
  warnPct?: number; cutPct?: number;
}) {
  const ls = loadStatus(f);
  const load = f?.actualLoad ?? 0;
  const cap = f?.safeLoadLimit ?? 0;
  const pct = ls === "ok" ? (f?.loadPercent ?? 0) : 0;
  const warnAt = warnPct, cutAt = cutPct;   // bands of the chart on the ESP32 (it decides the real states)

  const state = safety?.state;
  const level: Level = ls === "loadErr" ? "warn"
    : state === "CUT" || state === "FAULT" || state === "ESTOP" || pct >= cutAt ? "crit"
    : state === "WARN" || pct >= warnAt ? "warn" : "";
  // The NUMBER is coloured by the load itself; a FAULT (no chart / sensor down) shows on the chip only
  const loadLevel: Level = ls !== "ok" ? "" : state === "CUT" || pct >= cutAt ? "crit" : state === "WARN" || pct >= warnAt ? "warn" : "";
  const chipText = ls === "outside" ? "Outside chart" : ls === "loadErr" ? "Load sensor error"
    : state ?? (f ? "No SLI status" : "No data");

  const boom = f?.boomAngle ?? 0, ext = f?.extensionMM ?? 0;
  const radiusMm = hookArmMm(ext) * Math.cos((boom * Math.PI) / 180);   // pivot to the hook, horizontally
  const lean = f?.imuPitch ?? 0;
  const leanLevel: Level = Math.abs(lean) > 5 ? "crit" : Math.abs(lean) > 3 ? "warn" : "";

  return (
    <div className="panel tel-panel">
      <div className="panel-header">
        Crane telemetry
        <span className="ph-right">
          <span className={`state-chip ${level === "crit" ? "crit" : level === "warn" ? "warn" : f ? "ok" : "off"}`}>
            {level === "crit" ? "▲ " : level === "warn" ? "△ " : ""}{chipText}
          </span>
        </span>
      </div>

      <div className="tel-body">
        <div>
          <div className="tel-load-k">Hook load</div>
          <div className="tel-load-row">
            <span className={`tel-load-val ${loadLevel}`}>{ls === "loadErr" ? "ERR" : fmt(load, 3)}</span>
            <span className="tel-load-unit">kg</span>
            <span className="tel-load-of">rated <b>{cap > 0 ? fmt(cap, 3) : "–"}</b> kg</span>
          </div>
        </div>

        <div>
          <div className="loadbar" role="meter" aria-valuemin={0} aria-valuemax={BAR_MAX} aria-valuenow={pct}
               aria-label="Load percent of rated capacity">
            <div className="loadbar-band warn" style={{ left: `${(warnAt / BAR_MAX) * 100}%`, width: `${((cutAt - warnAt) / BAR_MAX) * 100}%` }} />
            <div className="loadbar-band crit" style={{ left: `${(cutAt / BAR_MAX) * 100}%`, right: 0 }} />
            <div className={`loadbar-fill ${loadLevel}`} style={{ width: `${Math.min(100, (pct / BAR_MAX) * 100)}%` }} />
            <div className="loadbar-tick" style={{ left: `${(cutAt / BAR_MAX) * 100}%` }} />
          </div>
          <div className="loadbar-scale"><span>0</span><span>{warnAt}</span><span>{cutAt}</span><span>{BAR_MAX} %</span></div>
          <div className="tel-pct-row">
            <span className="tel-pct">{ls === "ok" ? fmt(pct, 1) : "–"}<small>% of rated</small></span>
            <span className="tel-note">{ls === "outside" ? outsideReason(f, chart) : ls === "loadErr" ? "tare / calibrate in Debug" : `cell ${fmt(f?.measuredLoad, 3)} kg`}</span>
          </div>
        </div>

        {safety && safety.mode === "ARMED" && (safety.state === "CUT" || safety.state === "FAULT" || safety.state === "ESTOP") && (
          <div className="sli-notice" role="status">
            <div className="sli-notice-head">
              ▲ SLI {safety.state === "ESTOP" ? "E-STOP" : safety.state}: {ls === "outside" ? "outside the load chart" : safety.state === "FAULT" ? "only safe moves allowed" : safety.state === "ESTOP" ? "latched" : "overload"}
            </div>
            <div className="sli-notice-why">
              {ls === "outside" ? `${outsideReason(f, chart)}. ${outsideFix(f, chart)}` : reasonText(safety.state, safety)}
            </div>
            {safety.state !== "ESTOP" && (
              <div className="sli-notice-ok">Allowed now: {allowedMotions(safety).join(", ") || "nothing"}</div>
            )}
            {ls === "outside" && <StaleChart crane={chart} />}
          </div>
        )}

        <div className="tel-grid">
          <Tile k="Boom angle" v={fmt(boom)} unit="°" />
          <Tile k="Radius" v={f ? fmt(radiusMm, 0) : "–"} unit="mm" title="(288 mm bearing centre to load-cell edge + extension) × cos(boom angle): pivot to the hook, horizontally" />
          <Tile k="Extension" v={fmt(f?.extensionMM, 0)} unit="mm" />
          <Tile k="Swing" v={fmt(f?.swingAngle)} unit="°" />
          <Tile k="Hook drop" v={f && f.hookDrop > 0 ? fmt(f.hookDrop, 0) : "–"} unit="mm"
                title={`Load-cell end to hook tip. Calibrated rope: ${fmt(f?.ropeLength, 0)} mm`} />
          <Tile k="Rope total" v={f && f.ropeTotal > 0 ? fmt(f.ropeTotal, 0) : "–"} unit="mm"
                title="Calibrated rope (load-cell edge to knot) + 288 mm bearing centre to load-cell edge + 76 mm knot to hook" />
          <Tile k="Boom lean" v={fmt(lean)} unit="°" level={leanLevel} title="Sideways lean from the IMU: warning above 3°, alarm above 5°" />
          <Tile k="Boom-tip shock" v={fmt(f?.accelPeak, 2)} unit="m/s²"
                title="Acceleration above the idle level at the boom tip, peak of the last 2 s (0 at rest)" />
        </div>
      </div>
    </div>
  );
}
