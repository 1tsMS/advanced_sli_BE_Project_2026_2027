import { useEffect, useState } from "react";
import { Scale, Trash2 } from "lucide-react";
import { apiJson } from "../../api";
import type { DebugReport, TelemetryFrame } from "../../types";

interface Props {
  frame: TelemetryFrame | null;
  debugReport: DebugReport | null;
  /** Last $ACK line from the ESP32 (CAL3 replies arrive here) */
  lastAck: string | null;
}

// Friendly text for the ESP32's CAL3 error codes
const ERRORS: Record<string, string> = {
  NO_LOAD: "The load cell reads about zero. Is the weight hanging on the hook, and was the cell tared with nothing hanging?",
  GAIN_RANGE: "The reading is far from the weight you entered. Check the weight value and tare the load cell.",
  KNOWN_WEIGHT_RANGE: "The weight must be between 0.05 and 100 kg.",
  BOOM_MOVING: "The boom moved while measuring. Hold it still and try again.",
  LOAD_SENSOR_FAULT: "The load cell is not delivering readings.",
  TABLE_FULL: "All 8 angle points are used. Re-record one of the existing angles, or clear the table.",
  FLASH: "Saving to the ESP32's flash failed.",
  BUSY: "The ESP32 was busy. Try again.",
  UNAVAILABLE: "Not available in this firmware.",
};

/** Turn a "$ACK,CAL3,..." line into a message; null for anything else. */
function describeAck(ack: string | null): { ok: boolean; text: string } | null {
  if (!ack || !ack.startsWith("$ACK,CAL3")) return null;
  const parts = ack.split(",");
  if (parts[2] === "OK") {
    const angle = parts.find(p => p.startsWith("ANGLE:"))?.slice(6);
    const gain = parts.find(p => p.startsWith("GAIN:"))?.slice(5);
    return { ok: true, text: `Recorded: gain ${gain} at ${angle}°.` };
  }
  if (parts[2] === "CLEARED") return { ok: true, text: "All angle corrections cleared." };
  if (parts[2] === "ERR") return { ok: false, text: ERRORS[parts[3]] ?? `Failed: ${parts[3]}` };
  return null;
}

export function LoadGainCalibration({ frame, debugReport, lastAck }: Props) {
  const [weight, setWeight] = useState("1.000");
  const [sent, setSent] = useState<string>("");

  // Recorded points come from the ESP32's DBG report: $D,GAIN,<angle>,<gain>
  const points = (debugReport?.entries ?? [])
    .filter(e => e.bus === "GAIN")
    .map(e => ({ angle: parseFloat(e.address ?? ""), gain: parseFloat(e.status) }))
    .filter(p => Number.isFinite(p.angle) && Number.isFinite(p.gain));

  // When the ESP32 answers a CAL3 command, refresh the table
  useEffect(() => {
    if (lastAck?.startsWith("$ACK,CAL3")) void apiJson("/debug/scan", { method: "POST" });
  }, [lastAck]);

  const ack = describeAck(lastAck);
  const weightNum = Number(weight);
  const weightOk = Number.isFinite(weightNum) && weightNum >= 0.05 && weightNum <= 100;

  const record = async () => {
    setSent("Measuring for about half a second…");
    const res = await apiJson("/calibrate/loadgain", { body: { weightKg: weightNum } });
    if (!res.ok) setSent(res.error ?? "Could not send the command");
  };

  const clearAll = async () => {
    if (!window.confirm("Remove all recorded angle corrections?")) return;
    setSent("Clearing…");
    const res = await apiJson("/calibrate/loadgain/clear", { method: "POST" });
    if (!res.ok) setSent(res.error ?? "Could not send the command");
  };

  const measured = frame?.measuredLoad ?? 0;
  const corrected = frame?.actualLoad ?? 0;

  return (
    <div className="cal-item" style={{ border: "1px solid var(--border-accent)" }}>
      <div className="cal-item-top">
        <div>
          <div className="cal-item-name" style={{ color: "var(--cyan)", display: "flex", alignItems: "center", gap: 6 }}>
            <Scale size={13} />
            Load Cell Angle Correction
          </div>
          <div className="cal-item-sub">
            The load cell reads differently at different boom angles. Hang a known weight, hold the boom
            still, and record. Repeat at 2–4 angles across the boom's range. With nothing recorded, no
            correction is applied.
          </div>
        </div>
      </div>

      <div className="tele-cal-box">
        {/* Live readout */}
        <div className="tele-readout-strip">
          <div className="tele-readout-left">
            <div>
              <div style={{ fontSize: 9, color: "var(--text-muted)", textTransform: "uppercase", letterSpacing: 0.5 }}>Boom angle</div>
              <div className="tele-val-lg" style={{ color: "var(--cyan)" }}>
                {(frame?.boomAngle ?? 0).toFixed(1)} <span style={{ fontSize: 12, fontWeight: 500 }}>°</span>
              </div>
            </div>
            <div style={{ marginLeft: 16 }}>
              <div style={{ fontSize: 9, color: "var(--text-muted)", textTransform: "uppercase", letterSpacing: 0.5 }}>Measured</div>
              <div className="tele-val-sub" style={{ fontSize: 13, color: "var(--text-primary)", fontWeight: 600 }}>
                {measured.toFixed(3)} <span style={{ color: "var(--text-muted)", fontSize: 10 }}>kg</span>
              </div>
            </div>
            <div style={{ marginLeft: 16 }}>
              <div style={{ fontSize: 9, color: "var(--text-muted)", textTransform: "uppercase", letterSpacing: 0.5 }}>Corrected</div>
              <div className="tele-val-sub" style={{ fontSize: 13, color: "var(--green)", fontWeight: 600 }}>
                {corrected.toFixed(3)} <span style={{ color: "var(--text-muted)", fontSize: 10 }}>kg</span>
              </div>
            </div>
          </div>
        </div>

        {/* Record */}
        <div className="tele-section-card">
          <div className="tele-section-title">
            <span>Record at the current angle</span>
          </div>
          <div className="tele-input-row" style={{ marginTop: 4 }}>
            <span style={{ fontSize: 11, color: "var(--text-secondary)" }}>Known weight on the hook:</span>
            <input
              type="number" step="0.001" className="tele-input"
              value={weight} onChange={e => setWeight(e.target.value)}
            />
            <span style={{ fontSize: 11, color: "var(--text-muted)" }}>kg</span>
            <button
              className="btn-primary" onClick={record} disabled={!weightOk}
              style={{ marginLeft: "auto", padding: "5px 12px" }}
              title="Averages the load cell for about half a second and stores the correction in ESP32 flash"
            >
              Record
            </button>
          </div>
        </div>

        {/* Recorded points */}
        <div className="tele-section-card">
          <div className="tele-section-title">
            <span>Recorded points ({points.length}/8)</span>
            <button className="btn-ghost" onClick={clearAll} disabled={points.length === 0}
                    style={{ padding: "1px 6px", fontSize: 9, display: "inline-flex", alignItems: "center", gap: 4 }}>
              <Trash2 size={9} /> Clear all
            </button>
          </div>
          {points.length === 0 ? (
            <div className="dim-text" style={{ fontSize: 11, marginTop: 4 }}>
              None recorded, so no correction is applied. Press Scan above to refresh this list.
            </div>
          ) : (
            <table className="scan-table" style={{ marginTop: 4 }}>
              <thead>
                <tr><th>Boom angle</th><th>Cell reads</th><th>Correction</th></tr>
              </thead>
              <tbody>
                {points.map(p => (
                  <tr key={p.angle}>
                    <td>{p.angle.toFixed(1)}°</td>
                    <td className="dim-text">{(p.gain * 100).toFixed(1)}% of true weight</td>
                    <td className="dim-text">÷ {p.gain.toFixed(4)}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </div>

        {(ack || sent) && (
          <div className="cal-item-status" style={{ marginTop: 2, color: ack ? (ack.ok ? "var(--green)" : "var(--red)") : "var(--cyan)" }}>
            ← {ack ? ack.text : sent}
          </div>
        )}
      </div>
    </div>
  );
}
