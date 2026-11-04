import { useState } from "react";
import { RotateCcw, Ruler, Save, Plus, Trash2 } from "lucide-react";
import type { DebugReport, TelemetryFrame } from "../../types";
import { encoderDiag } from "./encoderDiag";
import { useHoldToMove } from "../../hooks/useHoldToMove";
import type { RulerAxisConfig } from "./rulerAxes";

interface RulerCalProps {
  cfg: RulerAxisConfig;
  frame: TelemetryFrame | null | undefined;
  debugReport: DebugReport | null;
  onScan: () => void;
  onLog: (text: string, type: "ack" | "info" | "err") => void;
}

/** One ruler measurement: encoder steps since zero vs. measured length. */
interface CalPoint { steps: number; measured: number; }

/** measured = offset + scale * steps */
interface Fit { scale: number; offset: number; }

/**
 * Least-squares line through the points. Several points average out ruler
 * reading error and backlash; the residuals show whether something is slipping
 * (they grow with travel if it is).
 * - withOffset false: through the origin, scale = Σ(m·s) / Σ(s²). Right when the
 *   zero position really measures 0 (telescope retracted = 0 mm).
 * - withOffset true: free offset, needs 2+ points spread apart. The zero then
 *   doesn't have to be exact (rope: the hook fully up is still some mm below the
 *   load cell; forcing the line through 0 there made the scale come out wrong).
 */
function fitLine(points: CalPoint[], withOffset: boolean): Fit | null {
  const n = points.length;
  if (!withOffset) {
    const sss = points.reduce((a, p) => a + p.steps * p.steps, 0);
    if (sss < 1e-9) return null;
    return { scale: points.reduce((a, p) => a + p.measured * p.steps, 0) / sss, offset: 0 };
  }
  if (n < 2) return null;
  const sMean = points.reduce((a, p) => a + p.steps, 0) / n;
  const mMean = points.reduce((a, p) => a + p.measured, 0) / n;
  const sxx = points.reduce((a, p) => a + (p.steps - sMean) ** 2, 0);
  if (sxx < 1e-9) return null;
  const scale = points.reduce((a, p) => a + (p.steps - sMean) * (p.measured - mMean), 0) / sxx;
  return { scale, offset: mMean - scale * sMean };
}

export function RulerCal({ cfg, frame, debugReport, onScan, onLog }: RulerCalProps) {
  const motion = useHoldToMove();
  const [points, setPoints] = useState<CalPoint[]>([]);
  const [measuredMm, setMeasuredMm] = useState("");
  const [manualScale, setManualScale] = useState<string | null>(null);   // null = show the ESP32's value
  const [manualInvert, setManualInvert] = useState<boolean | null>(null);
  const [jogSpeed, setJogSpeed] = useState(1000);
  const [joggingDir, setJoggingDir] = useState<0 | 1 | null>(null);

  // The ESP32's flash is the only source of truth for scale / direction
  const axis = encoderDiag(debugReport, cfg.diagTag).axis;
  const espScale = axis?.scale ?? null;
  const espInvert = axis?.inverted ?? null;
  const loaded = espScale !== null && espInvert !== null;
  const activeScale = espScale ?? cfg.defaultScale;

  const live = cfg.value(frame);
  // Signed encoder steps in the current direction (from the zero; any constant offset
  // is absorbed by the fit when cfg.fitOffset)
  const liveSteps = (cfg.stepValue ?? cfg.value)(frame) / activeScale;
  const withOffset = !!cfg.fitOffset;
  const fit = points.length > 0 ? fitLine(points, withOffset) : null;
  const minSteps = cfg.minSteps;
  const u = cfg.unit;

  const zero = async () => {
    onLog(`→ ${cfg.title}: zero`, "info");
    const r = await cfg.zero();
    onLog(`← ${r.msg}`, r.ok ? "ack" : "err");
    setPoints([]);
  };

  const startJog = (e: React.PointerEvent<HTMLButtonElement>, dir: 0 | 1) => {
    e.currentTarget.setPointerCapture(e.pointerId);
    setJoggingDir(dir);
    motion.hold(cfg.motorAxis, jogSpeed, dir);
  };
  const stopJog = () => {
    setJoggingDir(null);
    motion.release(cfg.motorAxis);
  };
  const jogProps = (dir: 0 | 1) => ({
    onPointerDown: (e: React.PointerEvent<HTMLButtonElement>) => startJog(e, dir),
    onPointerUp: stopJog,
    onPointerCancel: stopJog,
    onLostPointerCapture: stopJog,
  });

  const addPoint = () => {
    const m = parseFloat(measuredMm);
    if (!loaded) { onLog("⚠ Press Scan / Refresh first so the ESP32's scale is known", "err"); return; }
    if (!Number.isFinite(m) || m < 0 || (!withOffset && m === 0)) {
      onLog(`⚠ Enter the measured value in ${u} (${withOffset ? ">= 0" : "> 0"})`, "err"); return;
    }
    if (withOffset) {
      // Any first point; the others must be far enough from every point so far
      if (points.some(p => Math.abs(p.steps - liveSteps) < minSteps)) {
        onLog(`⚠ Move further: too close to a recorded point (< ${minSteps} ${cfg.stepName})`, "err"); return;
      }
    } else if (Math.abs(liveSteps) < minSteps) {
      onLog(`⚠ Move further out: too few ${cfg.stepName} since zero`, "err"); return;
    }
    setPoints(prev => [...prev, { steps: liveSteps, measured: m }]);
    setMeasuredMm("");
    onLog(`+ point: ${m.toFixed(1)} ${u} at ${liveSteps.toFixed(cfg.stepDigits)} ${cfg.stepName}`, "info");
  };

  const saveScale = async (scale: number, invert: boolean, what: string, value?: number) => {
    onLog(`→ ${what}: ${scale.toFixed(cfg.scaleDigits)} ${cfg.scaleUnit}, invert=${invert ? 1 : 0}` +
          (value !== undefined ? `, now ${value.toFixed(1)} ${u}` : ""), "info");
    const r = await cfg.save(scale, invert, value);
    onLog(`← ${r.msg}`, r.ok ? "ack" : "err");
    if (r.ok) {
      setPoints([]);            // their direction/scale basis just changed
      setManualScale(null);
      setManualInvert(null);
      setTimeout(onScan, 400);
    }
  };

  const saveFit = () => {
    if (fit === null || espInvert === null) return;
    // A negative fit means the encoder counts the other way: flip the direction.
    // Offset fits also send the length right now, so the reading matches the tape.
    saveScale(Math.abs(fit.scale), fit.scale < 0 ? !espInvert : espInvert, `Save fit (${points.length} points)`,
              withOffset ? fit.offset + fit.scale * liveSteps : undefined);
  };

  const saveManual = () => {
    const scale = parseFloat(manualScale ?? String(activeScale));
    if (!Number.isFinite(scale) || scale <= 0) { onLog("⚠ Scale must be greater than 0", "err"); return; }
    saveScale(scale, manualInvert ?? espInvert ?? cfg.defaultInvert, "Save manual scale");
  };

  const shownInvert = manualInvert ?? espInvert ?? cfg.defaultInvert;

  return (
    <div className="cal-item" style={{ border: "1px solid var(--border-accent)" }}>
      <div className="cal-item-top">
        <div>
          <div className="cal-item-name" style={{ color: "var(--cyan)", display: "flex", alignItems: "center", gap: 6 }}>
            <Ruler size={13} />
            {cfg.title}
          </div>
          <div className="cal-item-sub">{cfg.sub}</div>
        </div>
        <button className="btn-primary" style={{ padding: "4px 10px", fontSize: 10 }} onClick={zero} title={cfg.zeroHint}>
          <RotateCcw size={10} />
          {cfg.zeroLabel}
        </button>
      </div>

      <div className="tele-cal-box">
        <div className="tele-readout-strip">
          <div className="tele-readout-left">
            <div>
              <div className="tele-k">Live</div>
              <div className="tele-val-lg" style={{ color: live < -0.2 ? "var(--amber)" : "var(--cyan)" }}>
                {live >= 0 ? `+${live.toFixed(1)}` : live.toFixed(1)} <span style={{ fontSize: 12, fontWeight: 500 }}>{u}</span>
              </div>
            </div>
            <div style={{ marginLeft: 16 }}>
              <div className="tele-k">{cfg.stepName}</div>
              <div className="tele-val-sub" style={{ fontSize: 13, color: "var(--text-primary)", fontWeight: 600 }}>
                {liveSteps >= 0 ? "+" : ""}{liveSteps.toFixed(cfg.stepDigits)}
              </div>
            </div>
          </div>
          <div style={{ textAlign: "right" }}>
            <div className="tele-k">On the ESP32</div>
            <div style={{ fontFamily: "var(--font-mono)", fontSize: 11, color: loaded ? "var(--text-secondary)" : "var(--amber)" }}>
              {loaded ? `${espScale!.toFixed(cfg.scaleDigits)} ${cfg.scaleUnit} · inv ${espInvert ? 1 : 0}` : "not loaded: Scan"}
            </div>
          </div>
        </div>

        <div className="tele-section-card">
          <div className="tele-section-title">
            <span>Jog (M{cfg.motorAxis})</span>
            <div style={{ display: "flex", gap: 4, alignItems: "center" }}>
              {[500, 1000, 2000].map(s => (
                <button key={s} className={`axis-badge ${jogSpeed === s ? "sel" : ""}`}
                        style={{ padding: "1px 6px", fontSize: 9 }} onClick={() => setJogSpeed(s)}>
                  {s}
                </button>
              ))}
            </div>
          </div>
          <div style={{ display: "flex", gap: 6 }}>
            <button className={`tele-jog-btn ${joggingDir === (1 - cfg.outDir) ? "pressed" : ""}`}
                    {...jogProps((1 - cfg.outDir) as 0 | 1)}>
              {cfg.inLabel}
            </button>
            <button className={`tele-jog-btn ${joggingDir === cfg.outDir ? "pressed" : ""}`}
                    {...jogProps(cfg.outDir)}>
              {cfg.outLabel}
            </button>
            <button className="tele-jog-btn stop" onClick={stopJog} title={`Stop axis ${cfg.motorAxis}`}>■</button>
          </div>
        </div>

        <div className="tele-section-card">
          <div className="tele-section-title">
            <span>Ruler Points</span>
            {points.length > 0 && (
              <button className="btn-ghost" style={{ padding: "1px 6px", fontSize: 9 }} onClick={() => setPoints([])}>
                <Trash2 size={9} /> Clear
              </button>
            )}
          </div>
          <div className="tele-input-row">
            <span style={{ fontSize: 11, color: "var(--text-secondary)" }}>Measured</span>
            <input type="number" step="0.1" className="tele-input" value={measuredMm} placeholder={u}
                   aria-label={`${cfg.title} measured length`}
                   onChange={e => setMeasuredMm(e.target.value)}
                   onKeyDown={e => { if (e.key === "Enter") addPoint(); }} />
            <span style={{ fontSize: 11, color: "var(--text-muted)" }}>{u}</span>
            <button className="btn-ghost" onClick={addPoint} style={{ padding: "4px 10px" }}>
              <Plus size={10} /> Record point
            </button>
          </div>

          {points.length > 0 && (
            <table className="scan-table tele-points">
              <thead>
                <tr><th>#</th><th>{cfg.stepName}</th><th>Ruler</th><th>Fit says</th><th>Error</th></tr>
              </thead>
              <tbody>
                {points.map((p, i) => {
                  const predicted = fit !== null ? fit.offset + fit.scale * p.steps : null;
                  const err = predicted !== null ? p.measured - predicted : null;
                  return (
                    <tr key={i}>
                      <td className="dim-text">{i + 1}</td>
                      <td>{p.steps.toFixed(cfg.stepDigits)}</td>
                      <td>{p.measured.toFixed(1)} {u}</td>
                      <td>{predicted !== null ? `${predicted.toFixed(1)} ${u}` : "—"}</td>
                      <td className={err !== null && Math.abs(err) > 1 ? "fail-text" : "ok-text"}>
                        {err !== null ? `${err >= 0 ? "+" : ""}${err.toFixed(2)}` : "—"}
                      </td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
          )}

          <div className="tele-input-row">
            <span style={{ fontSize: 11, color: "var(--text-secondary)" }}>
              Fitted scale:{" "}
              <span style={{ fontFamily: "var(--font-mono)", color: fit !== null ? "var(--cyan)" : "var(--text-muted)" }}>
                {fit !== null
                  ? `${Math.abs(fit.scale).toFixed(cfg.scaleDigits)} ${cfg.scaleUnit}${fit.scale < 0 ? " (direction flips)" : ""}` +
                    (withOffset ? ` · offset ${fit.offset.toFixed(1)} ${u}` : "")
                  : withOffset ? "record 2+ points" : "record a point"}
              </span>
            </span>
            <button className="btn-primary" onClick={saveFit} disabled={fit === null || !loaded}
                    style={{ marginLeft: "auto", padding: "5px 12px" }}
                    title="Write the fitted scale (and direction) to the ESP32's flash">
              <Save size={10} /> Save fit
            </button>
          </div>
        </div>

        <div className="tele-section-card">
          <div className="tele-section-title">
            <span>Manual Scale</span>
            <button className="btn-ghost" style={{ padding: "1px 6px", fontSize: 9 }}
                    onClick={() => saveScale(cfg.defaultScale, cfg.defaultInvert, "Reset to defaults")}>
              Reset Defaults
            </button>
          </div>
          <div className="tele-input-row">
            <input type="number" step="any" className="tele-input"
                   value={manualScale ?? (espScale !== null ? espScale.toFixed(cfg.scaleDigits) : String(cfg.defaultScale))}
                   onChange={e => setManualScale(e.target.value)} />
            <span style={{ fontSize: 11, color: "var(--text-muted)" }}>{cfg.scaleUnit}</span>
            <button className={`axis-badge ${shownInvert ? "sel" : ""}`} style={{ marginLeft: 6 }}
                    onClick={() => setManualInvert(!shownInvert)}>
              Invert: {shownInvert ? "YES" : "NO"}
            </button>
            <button className="btn-primary" onClick={saveManual} style={{ marginLeft: "auto", padding: "5px 12px" }}>
              <Save size={10} /> Save to Flash
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
