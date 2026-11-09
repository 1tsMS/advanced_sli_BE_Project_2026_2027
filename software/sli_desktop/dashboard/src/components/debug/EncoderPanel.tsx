import { useState } from "react";
import { RefreshCw } from "lucide-react";
import type { DebugReport, TelemetryFrame } from "../../types";
import { ENCODER_AXES, encoderDiag, type EncoderAxisId } from "./encoderDiag";
import { API_BASE } from "../../api";
import { TELE_RETRACTED_MM } from "../../geometry";


interface EncoderPanelProps {
  frame: TelemetryFrame | null | undefined;
  debugReport: DebugReport | null;
  onScan: () => void;
  onLog: (text: string, type: "ack" | "info" | "err") => void;
}

const MAGNET_HINT: Record<string, string> = {
  OK:         "Magnet field in range",
  TOO_WEAK:   "Field too weak: magnet too far from the chip or off-centre",
  TOO_STRONG: "Field too strong: magnet too close to the chip",
  NO_MAGNET:  "No magnet detected",
  NO_DATA:    "Not read yet (or sensor offline)",
};

function liveValue(id: EncoderAxisId, f: TelemetryFrame | null | undefined): number | null {
  if (!f) return null;
  if (id === "swing") return f.swingAngle;
  if (id === "boom")  return f.boomAngle;
  if (id === "rope")  return f.ropeLength;
  return f.extensionMM;
}

/**
 * Per-encoder health and calibration: magnet placement (AS5600 STATUS/AGC),
 * I2C link quality, and zero / direction stored in the ESP32's flash.
 */
export function EncoderPanel({ frame, debugReport, onScan, onLog }: EncoderPanelProps) {
  // Typed "current position" per axis; empty means 0
  // Same references as the Calibrate steps: swing home 0°, boom rest 0°, telescope retracted, hook fully up 0 mm
  const [refInput, setRefInput] = useState<Record<EncoderAxisId, string>>(
    { swing: "0", boom: "0", tele: String(TELE_RETRACTED_MM), rope: "0" });
  const [busy, setBusy] = useState<EncoderAxisId | null>(null);

  const send = async (axis: EncoderAxisId, body: { value?: number; invert?: boolean }) => {
    setBusy(axis);
    onLog(`→ ${axis}: ${body.invert !== undefined ? `invert=${body.invert ? 1 : 0} ` : ""}${body.value !== undefined ? `set to ${body.value}` : ""}`, "info");
    try {
      const res = await fetch(`${API_BASE}/calibrate/encoder`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ axis, ...body }),
      });
      const data = await res.json().catch(() => ({}));
      onLog(`← ${data.message ?? data.detail ?? (res.ok ? "Done" : "Failed")}`, res.ok ? "ack" : "err");
    } catch {
      onLog("← Backend offline", "err");
    } finally {
      setBusy(null);
      // Read back what the ESP32 now has stored
      setTimeout(onScan, 400);
    }
  };

  return (
    <div className="panel enc-card">
      <div className="panel-header">
        <span className="ph-title">Encoders</span>
        <button className="btn-ghost btn-sm" onClick={onScan} title="Re-read diagnostics from the ESP32 (DBG)">
          <RefreshCw size={10} /> Refresh
        </button>
      </div>
      <div className="panel-body enc-body">
        {!debugReport && (
          <div className="enc-empty">Press Refresh to read magnet health and the stored calibration.</div>
        )}

        {ENCODER_AXES.map(ax => {
          const d = encoderDiag(debugReport, ax.tag);
          const v = liveValue(ax.id, frame);
          const failPct = d.link && d.link.reads > 0 ? (100 * d.link.fails) / d.link.reads : 0;
          const verdict = d.magnet?.verdict ?? "NO_DATA";
          const magClass = verdict === "OK" ? "ok" : verdict === "NO_DATA" ? "dim" : "bad";

          return (
            <div className="enc-row" key={ax.id}>
              <div className="enc-row-top">
                <span className="enc-name">{ax.label}</span>
                {ax.id === "boom" && d.axis?.source && (
                  <span className={`enc-tag ${d.axis.source === "ENC" ? "ok" : "warn"}`}
                        title={d.axis.source === "ENC" ? "Boom angle comes from this encoder" : "Boom angle comes from the IMU. The boom encoder is on the leadscrew motor (non-linear), so it is only shown in DBG for now (BOOM_ANGLE_FROM_ENCODER in config.h)."}>
                    source: {d.axis.source === "ENC" ? "encoder" : "IMU"}
                  </span>
                )}
                <span className="enc-live">
                  {v === null ? "--" : v.toFixed(ax.unit === "mm" ? 1 : 2)}<span className="enc-unit">{ax.unit}</span>
                </span>
              </div>

              {ax.kind === "n20" ? (
              <div className="enc-diag">
                <span className="enc-tag dim" title="N20 quadrature encoder, counted in hardware (ESP32 PCNT, 4x, glitch filter)">
                  {d.axis?.counts !== null && d.axis?.counts !== undefined ? `${d.axis.counts} counts` : "counts: scan"}
                </span>
              </div>
              ) : (
              <div className="enc-diag">
                <span className={`enc-tag ${magClass}`} title={MAGNET_HINT[verdict] ?? verdict}>
                  magnet {verdict.replace("_", " ").toLowerCase()}
                </span>
                {d.magnet?.agc !== null && d.magnet?.agc !== undefined && (
                  <span className="enc-agc" title="Automatic gain. Mid-range is best: near 0 = field too strong, near max = too weak (max is 128 at 3.3 V, 255 at 5 V)">
                    AGC
                    <span className="enc-agc-track">
                      <span className="enc-agc-fill" style={{ width: `${Math.min(100, (d.magnet.agc / 255) * 100)}%` }} />
                    </span>
                    {d.magnet.agc}
                  </span>
                )}
                {d.link && (
                  <span className={`enc-tag ${d.link.status === "OK" ? "dim" : "bad"}`}
                        title={`${d.link.fails} of ${d.link.reads} I2C reads failed; ${d.link.rejected} samples rejected as glitches; ${d.link.resync} resyncs (counts lost)`}>
                    I2C {failPct < 0.05 ? "clean" : `${failPct.toFixed(1)}% errors`}
                    {d.link.resync > 0 ? ` · ${d.link.resync} resync` : ""}
                  </span>
                )}
              </div>
              )}

              <div className="enc-cal">
                <span className="enc-cal-label">Now at</span>
                <input
                  type="number"
                  step={ax.unit === "mm" ? "0.1" : "0.5"}
                  className="tele-input enc-input"
                  value={refInput[ax.id]}
                  onChange={e => setRefInput(prev => ({ ...prev, [ax.id]: e.target.value }))}
                  aria-label={`${ax.label} current position`}
                />
                <span className="enc-cal-label">{ax.unit}</span>
                <button
                  className="btn-primary enc-btn"
                  disabled={busy !== null}
                  onClick={() => {
                    const val = parseFloat(refInput[ax.id] || "0");
                    if (Number.isFinite(val)) send(ax.id, { value: val });
                    else onLog(`⚠ ${ax.label}: enter a number`, "err");
                  }}
                  title={ax.id === "boom"
                    ? "Sets the boom ENCODER reference (motor turns). The boom angle shown comes from the IMU; zero that with Zero IMU."
                    : ax.id === "tele"
                      ? "Telescope position right now (0 = fully retracted). Saved and restored across reboots."
                      : ax.id === "rope"
                        ? "Rope paid out right now (0 = hook fully up). Saved across reboots."
                        : "Swing position right now (0 = home). Invert first, then Set."}
                >
                  Set
                </button>
                <button
                  className={`axis-badge enc-inv ${d.axis?.inverted ? "sel" : ""}`}
                  disabled={busy !== null || !d.axis}
                  onClick={() => send(ax.id, { invert: !d.axis?.inverted })}
                  title="Flip the direction (mirrors around the zero). Read from the ESP32."
                >
                  INV {d.axis ? (d.axis.inverted ? "ON" : "OFF") : "?"}
                </button>
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
}
