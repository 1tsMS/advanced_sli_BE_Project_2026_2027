import { useState } from "react";
import { Plus, Trash2, Save } from "lucide-react";
import type { DebugReport, TelemetryFrame } from "../../types";
import { API_BASE, sendCommand } from "../../api";
import { CalSteps } from "./CalSection";
import { LOAD_CELL_FAULTS, loadCellVerdict } from "./loadCellHealth";


interface LoadCalProps {
  frame: TelemetryFrame | null | undefined;
  debugReport: DebugReport | null;
  lastAck: string | null;
  onScan: () => void;
  onLog: (text: string, type: "ack" | "info" | "err") => void;
}

async function apiPost(path: string, body: object) {
  try {
    const res = await fetch(`${API_BASE}${path}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
    const data = await res.json().catch(() => ({}));
    return { ok: res.ok, msg: data.message ?? data.detail ?? (res.ok ? "Done" : "Failed") };
  } catch {
    return { ok: false, msg: "Backend offline" };
  }
}

/** "key:value key:value" -> record */
function kv(s: string | null | undefined): Record<string, string> {
  const out: Record<string, string> = {};
  for (const part of (s ?? "").split(/[\s,]+/)) {
    const i = part.indexOf(":");
    if (i > 0) out[part.slice(0, i)] = part.slice(i + 1);
  }
  return out;
}

/**
 * Load cell calibration with known weights.
 * 1. Tare with nothing hanging. 2. Hang a weighed object, enter its kg, Record:
 * the ESP32 averages ~2 s of readings and refits counts-per-kg over every
 * weight recorded so far (least squares through zero), then saves it.
 * 3. Repeat with 1-2 more weights; the worst error tells you how good the fit is.
 */
export function LoadCal({ frame, debugReport, lastAck, onScan, onLog }: LoadCalProps) {
  const [weightKg, setWeightKg] = useState("");
  const [manualScale, setManualScale] = useState("");
  const [busy, setBusy] = useState(false);
  const [angErr, setAngErr] = useState<string | null>(null);   // shown in the card: the command log is elsewhere

  const line = debugReport?.entries.find(e => e.bus === "LOAD_CAL");
  const cal = kv(line?.address);
  const espScale = cal.scale ? parseFloat(cal.scale) : null;
  const isDefault = line?.status === "DEFAULT";

  // Latest CAL7 result straight from the ESP32's ACK
  const ack = lastAck && lastAck.startsWith("$ACK,CAL7") ? lastAck : null;
  const ackKv = kv(ack);
  const measuring = !!ack && ack.includes("MEASURING");

  const live = frame?.measuredLoad ?? 0;
  const corrected = frame?.actualLoad ?? 0;
  const boom = frame?.boomAngle ?? 0;

  // Boom-angle correction (CAL9): same known mass captured at 2+ boom angles
  const angAck = lastAck && lastAck.startsWith("$ACK,CAL9") ? lastAck : null;
  const angKv = kv(angAck);
  const angMeasuring = !!angAck && angAck.includes("MEASURING");
  // CAL9 errors in words. A refused capture never deletes the angles already captured.
  const angErrText = (a: string): string | null => {
    if (!a.includes(",ERR")) return null;
    const kept = a.match(/KEPT:(\d+)/)?.[1];
    const keptTxt = kept !== undefined ? ` The ${kept} angle point(s) already captured are kept.` : "";
    if (a.includes("NO_BOOM_ANGLE")) return `Not captured: the IMU dropped out (12 V noise) at the end of the 2 s average.${keptTxt} Press Capture again.`;
    if (a.includes("MOVED"))         return `Not captured: the boom moved during the 2 s average.${keptTxt} Hold still and capture again.`;
    if (a.includes("NO_LOAD_SEEN"))  return `Not captured: the load cell sees no weight. Tare with the hook empty, hang the weight, then capture.${keptTxt}`;
    if (a.includes("BUSY"))          return "Not captured: a measurement is still running. Wait 2 s and press again.";
    if (a.includes("TIMEOUT"))       return `Not captured: the load cell gave no readings for 5 s (check the HX711).${keptTxt}`;
    return a.replace("$ACK,CAL9,", "");
  };
  const sendAng = async (cmd: string, what: string) => {
    setBusy(true);
    onLog(`→ ${what}`, "info");
    const r = await sendCommand(cmd);
    onLog(`← ${r.message ?? (r.ok ? "Sent" : "Failed")}`, r.ok ? "ack" : "err");
    setAngErr(r.ok ? null : `Not sent: ${r.message ?? "backend refused"} (restart the backend?)`);
    setBusy(false);
  };
  const captureAngle = () => {
    const w = parseFloat(weightKg);
    if (!Number.isFinite(w) || w <= 0 || w > 10) { setAngErr("Type the weight in kg in the Known weight box first"); return; }
    sendAng(`CAL9 W${w.toFixed(4)}`, `Capture ${w} kg at boom ${boom.toFixed(1)}° (hold still ~2 s)`);
  };

  const send = async (body: object, what: string) => {
    setBusy(true);
    onLog(`→ ${what}`, "info");
    const r = await apiPost("/calibrate/load", body);
    onLog(`← ${r.msg}`, r.ok ? "ack" : "err");
    setBusy(false);
    setTimeout(onScan, 2600);   // read the new scale back after the ~2 s average
  };

  const tare = async () => {
    onLog("→ Tare (nothing hanging)", "info");
    const r = await apiPost("/calibrate/tare", {});
    onLog(`← ${r.msg}`, r.ok ? "ack" : "err");
  };

  const record = () => {
    const w = parseFloat(weightKg);
    if (!Number.isFinite(w) || w <= 0 || w > 10) { onLog("⚠ Enter the weight in kg (0–10)", "err"); return; }
    send({ action: "point", weight: w }, `Record ${w} kg (hold still ~2 s)`);
  };

  const saveManual = () => {
    const s = parseFloat(manualScale);
    if (!Number.isFinite(s) || s === 0) { onLog("⚠ Scale must be a non-zero number", "err"); return; }
    send({ action: "scale", scale: s }, `Set scale ${s} counts/kg`);
  };

  return (
    <div className="cal-body">
      {(() => {
        const v = loadCellVerdict(debugReport);
        const f = v ? LOAD_CELL_FAULTS[v] : undefined;
        return f ? (
          <div className="cal-fault" role="alert">
            <b>▲ {f.title}</b>
            <span>{f.check}</span>
            <span className="cal-fault-note">The SLI treats the load as unknown (FAULT) until this clears. Press Scan to re-check.</span>
          </div>
        ) : null;
      })()}
      <CalSteps
        steps={[
          "Empty hook, then Tare.",
          "Hang a weighed object, enter its kg, Record weight (hold still ~2 s).",
          "Repeat with 1-2 heavier weights. Each Record refits and saves the scale.",
        ]}
        action={<button className="btn-primary" onClick={tare} title="Zero the load cell with nothing hanging on the hook">Tare</button>}
      />

      <div className="tele-cal-box">
        <div className="tele-readout-strip">
          <div>
            <div className="tele-k">Live load</div>
            <div className="tele-val-lg" style={{ color: "var(--text)" }}>
              {live.toFixed(3)} <span style={{ fontSize: "var(--fs-md)", fontWeight: 500 }}>kg</span>
            </div>
          </div>
          <div style={{ textAlign: "right" }}>
            <div className="tele-k">Scale on the ESP32</div>
            <div style={{ fontFamily: "var(--font-mono)", fontSize: "var(--fs-sm)",
                          color: espScale === null ? "var(--amber)" : isDefault ? "var(--amber)" : "var(--text-secondary)" }}>
              {espScale === null ? "not loaded: Scan" : `${espScale.toFixed(1)} counts/kg${isDefault ? " (default guess)" : ""}`}
            </div>
            {cal.points && cal.points !== "0" && (
              <div style={{ fontSize: "var(--fs-xs)", color: "var(--text-muted)" }}>{cal.points} weight(s) this session</div>
            )}
          </div>
        </div>

        <div className="tele-section-card">
          <div className="tele-section-title">
            <span>Known weight</span>
            <button className="btn-ghost" style={{ padding: "1px 6px", fontSize: "var(--fs-xs)" }} disabled={busy}
                    onClick={() => send({ action: "clear" }, "Clear recorded weights")}>
              <Trash2 size={9} /> Clear points
            </button>
          </div>
          <div className="tele-input-row">
            <input type="number" step="0.001" className="tele-input" value={weightKg} placeholder="kg"
                   aria-label="Known weight in kg"
                   onChange={e => setWeightKg(e.target.value)}
                   onKeyDown={e => { if (e.key === "Enter") record(); }} />
            <span style={{ fontSize: "var(--fs-sm)", color: "var(--text-muted)" }}>kg</span>
            <button className="btn-primary" onClick={record} disabled={busy || measuring}
                    style={{ marginLeft: "auto", padding: "5px 12px" }}>
              <Plus size={10} /> {measuring ? "Measuring…" : "Record weight"}
            </button>
          </div>
          {ack && (
            <div className="cal-item-status" style={{ color: ack.includes(",ERR") ? "var(--amber)" : "var(--cyan)" }}>
              {ack.includes(",OK,")
                ? `Fitted ${ackKv.SCALE} counts/kg from ${ackKv.POINTS} weight(s) · worst error ${ackKv.WORST_ERR_G} g`
                : ack.replace("$ACK,CAL7,", "")}
            </div>
          )}
        </div>

        <div className="tele-section-card">
          <div className="tele-section-title">
            <span>Boom-angle correction</span>
            <button className="btn-ghost" style={{ padding: "1px 6px", fontSize: "var(--fs-xs)" }} disabled={busy}
                    onClick={() => sendAng("CAL9 CLEAR", "Clear angle points")}>
              <Trash2 size={9} /> Clear points
            </button>
          </div>
          <div style={{ fontSize: "var(--fs-xs)", color: "var(--text-muted)" }}>
            Same weight as above. Capture at a low boom angle, then again 15°+ higher. Boom now {boom.toFixed(1)}° · corrected load {corrected.toFixed(3)} kg
          </div>
          <div className="tele-input-row">
            <button className="btn-primary" onClick={captureAngle} disabled={busy || angMeasuring || measuring}
                    style={{ padding: "5px 12px" }}>
              <Plus size={10} /> {angMeasuring ? "Measuring…" : "Capture at this angle"}
            </button>
            <button className="btn-ghost" onClick={() => sendAng("CAL9 OFF", "Turn angle correction off")} disabled={busy}
                    style={{ marginLeft: "auto", padding: "5px 12px" }}>
              Off
            </button>
          </div>
          {angErr && (
            <div className="cal-item-status" style={{ color: "var(--amber)" }}>{angErr}</div>
          )}
          {angAck && (
            <div className="cal-item-status" style={{ color: angAck.includes(",ERR") ? "var(--amber)" : "var(--cyan)" }}>
              {angAck.includes(",OK,")
                ? `phi ${angKv.PHI}° · ${angKv.SCALE} counts/kg from ${angKv.N} angle(s) · worst error ${angKv.WORST_ERR_G} g`
                : angErrText(angAck) ?? angAck.replace("$ACK,CAL9,", "")}
            </div>
          )}
        </div>

        <div className="tele-section-card">
          <div className="tele-section-title"><span>Manual scale</span></div>
          <div className="tele-input-row">
            <input type="number" step="any" className="tele-input" value={manualScale}
                   placeholder={espScale !== null ? espScale.toFixed(1) : "counts/kg"}
                   onChange={e => setManualScale(e.target.value)} />
            <span style={{ fontSize: "var(--fs-sm)", color: "var(--text-muted)" }}>counts/kg</span>
            <button className="btn-ghost" onClick={saveManual} disabled={busy}
                    style={{ marginLeft: "auto", padding: "5px 12px" }}>
              <Save size={10} /> Save
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
