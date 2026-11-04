import { useState, useEffect, useRef } from "react";
import { RefreshCw } from "lucide-react";
import type { TelemetryFrame, DebugReport } from "../../types";
import { EncoderPanel } from "./EncoderPanel";
import { RulerCal } from "./RulerCal";
import { LoadCal } from "./LoadCal";
import { TELE_RULER, ROPE_RULER, SWING_RULER } from "./rulerAxes";
import { isEncoderDetailLine } from "./encoderDiag";

const API_BASE = "http://localhost:8000/api";

interface DebugTabProps {
  debugReport: DebugReport | null;
  lastAck: string | null;
  frame?: TelemetryFrame | null;
}

// ---- Human-readable device names + GPIO info ----
const DEVICE_INFO: Record<string, { name: string; gpio: string }> = {
  "I2C0_SWING":   { name: "AS5600 Swing Encoder",    gpio: "SDA=32 SCL=33" },
  "I2C1_BOOM":    { name: "AS5600 Boom Encoder",     gpio: "SDA=25 SCL=26" },
  "I2C2_TELE":    { name: "AS5600 Telescope Enc.",   gpio: "SDA=15 SCL=5" },
  "MPU6050":      { name: "MPU6050 (IMU)",           gpio: "SDA=21 SCL=22" },
  "HX711":        { name: "HX711 (Load Cell)",       gpio: "DT=17 SCK=16"  },
  "HX711_RAW":    { name: "HX711 raw counts",        gpio: "DT=17 SCK=16"  },
  "FSR1":         { name: "FSR Front-Left",          gpio: "ADC=34" },
  "FSR2":         { name: "FSR Front-Right",         gpio: "ADC=35" },
  "FSR3":         { name: "FSR Rear-Left",           gpio: "ADC=36" },
  "FSR4":         { name: "FSR Rear-Right",          gpio: "ADC=39" },
  "N20_TICKS":    { name: "N20 Winch Ticks",         gpio: "A=18 B=19" },
  "SENSOR_LOOP":  { name: "Sensor loop timing",      gpio: "core 1" },
  "LOOP_STAGES":  { name: "Slowest loop stage (ms)", gpio: "core 1" },
  "IMU_DROPOUTS": { name: "IMU dropouts / bad samples", gpio: "SDA=21 SCL=22" },
  "STACKS":       { name: "Task stack free (bytes)",  gpio: "RTOS" },
  "SERIAL_TX":    { name: "Serial lines dropped",     gpio: "RTOS" },
  "NVS":          { name: "Flash writes",             gpio: "RTOS" },
  "RESET":        { name: "Last reset / free heap",   gpio: "RTOS" },
  "LOAD_CAL":     { name: "Load cell scale",          gpio: "DT=17 SCK=16" },
};

const friendlyName = (bus: string) => DEVICE_INFO[bus]?.name ?? bus;
const gpioNote = (bus: string) => DEVICE_INFO[bus]?.gpio ?? "—";

// IMU zero (CAL1). The firmware supports X (roll) and Y (pitch) as sources.
type AxisChoice = "X" | "Y";
const CAL_ITEMS = [
  { key: "imu",  label: "Zero IMU / Gyro", sub: "Hold the boom still at its zero pose. Pick which IMU axis gives each angle.", path: "/calibrate/imu", hasAxis: true },
];

interface CalState { status: string; boomAxis: AxisChoice; swingAxis: AxisChoice; boomInv: boolean; swingInv: boolean; }

const CAL_STORAGE_KEY = "sli_debug_cal_state_v3";

const DEFAULT_CAL_STATE: Record<string, CalState> = {
  imu: { status: "", boomAxis: "X", swingAxis: "Y", boomInv: false, swingInv: false },
};

function loadSavedCalState(): Record<string, CalState> {
  try {
    const raw = localStorage.getItem(CAL_STORAGE_KEY);
    if (raw) return { ...DEFAULT_CAL_STATE, ...JSON.parse(raw) };
  } catch { /* storage blocked: use defaults */ }
  return DEFAULT_CAL_STATE;
}

async function apiPost(path: string, body: object = {}) {
  try {
    const res = await fetch(`${API_BASE}${path}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
    const data = await res.json();
    return { ok: res.ok, msg: data.message ?? data.detail ?? (res.ok ? "Done ✓" : "Failed") };
  } catch {
    return { ok: false, msg: "Backend offline" };
  }
}

export function DebugTab({ debugReport, lastAck, frame }: DebugTabProps) {
  const [scanning, setScanning] = useState(false);
  const [logs, setLogs] = useState<{ text: string; type: "ack" | "info" | "err" }[]>([]);
  const [calState, setCalState] = useState<Record<string, CalState>>(loadSavedCalState);
  const termRef = useRef<HTMLDivElement>(null);

  // Read diagnostics + stored calibration as soon as the tab opens (no state set here)
  useEffect(() => { apiPost("/debug/scan"); }, []);

  // Collect ACKs into the log
  useEffect(() => {
    if (!lastAck) return;
    setLogs(prev => [{ text: lastAck, type: "ack" as const }, ...prev].slice(0, 200));
  }, [lastAck]);

  const addLog = (text: string, type: "ack" | "info" | "err") => {
    const ts = new Date().toLocaleTimeString("en", { hour12: false, hour: "2-digit", minute: "2-digit", second: "2-digit" });
    setLogs(prev => [{ text: `[${ts}] ${text}`, type }, ...prev].slice(0, 200));
  };

  const scan = async () => {
    setScanning(true);
    addLog("→ DBG (status + diagnostics requested)", "info");
    await apiPost("/debug/scan");
    setTimeout(() => setScanning(false), 1500);
  };

  const runCal = async (item: typeof CAL_ITEMS[0]) => {
    const s = calState[item.key] ?? DEFAULT_CAL_STATE[item.key];
    const body = item.hasAxis ? {
      boomAxis:   s?.boomAxis  ?? "X",
      tiltAxis:   s?.swingAxis ?? "Y",
      boomInvert: s?.boomInv  ?? false,
      tiltInvert: s?.swingInv ?? false,
    } : {};

    addLog(`→ ${item.label} (${item.path})`, "info");
    const result = await apiPost(item.path, body);
    addLog(`← ${result.msg}`, result.ok ? "ack" : "err");
    setCalState(prev => {
      const updated = {
        ...prev,
        [item.key]: { ...(prev[item.key] ?? DEFAULT_CAL_STATE[item.key]), status: result.msg },
      };
      try { localStorage.setItem(CAL_STORAGE_KEY, JSON.stringify(updated)); } catch { /* ignore */ }
      return updated;
    });
  };

  const setCalAxis = (key: string, field: keyof CalState, val: string | boolean) => {
    setCalState(prev => {
      const cur = prev[key] ?? DEFAULT_CAL_STATE[key];
      const updated = { ...prev, [key]: { ...cur, [field]: val } };
      try { localStorage.setItem(CAL_STORAGE_KEY, JSON.stringify(updated)); } catch { /* ignore */ }
      return updated;
    });
  };

  const tableEntries = debugReport?.entries.filter(e => !isEncoderDetailLine(e.bus)) ?? [];

  return (
    <div className="debug-page">
      <div className="debug-main">

        {/* Left column: hardware status + encoders */}
        <div className="debug-col">
          <div className="debug-card debug-card-status">
            <div className="debug-card-header">
              <span className="debug-card-title">Hardware Status</span>
              <button
                className="btn-ghost"
                onClick={scan}
                disabled={scanning}
                style={{ display: "flex", alignItems: "center", gap: 5, padding: "3px 8px", fontSize: 10 }}
              >
                <RefreshCw size={10} style={{ animation: scanning ? "spin 1s linear infinite" : "none" }} />
                {scanning ? "Scanning..." : "Scan"}
              </button>
            </div>
            <div className="debug-card-body">
              {tableEntries.length > 0 ? (
                <table className="scan-table">
                  <thead>
                    <tr>
                      <th>Device</th>
                      <th>GPIO</th>
                      <th>Detail</th>
                      <th>Status</th>
                    </tr>
                  </thead>
                  <tbody>
                    {tableEntries.map((e, i) => {
                      const isFail = ["FAIL", "ERR", "NOT FOUND", "SLOW", "NOISY", "LOW", "DROPS", "DEFAULT"].includes(e.status);
                      const isNumeric = e.status !== "—" && e.status !== "" && !isNaN(Number(e.status));
                      const isOk = e.status === "OK" || (!isFail && isNumeric);
                      return (
                        <tr key={i}>
                          <td>{friendlyName(e.bus)}</td>
                          <td className="dim-text">{gpioNote(e.bus)}</td>
                          <td className="dim-text">{e.address ?? "—"}</td>
                          <td className={isOk ? "ok-text" : isFail ? "fail-text" : "dim-text"}>{e.status}</td>
                        </tr>
                      );
                    })}
                  </tbody>
                </table>
              ) : (
                <div style={{ fontSize: 11, color: "var(--text-muted)", paddingTop: 4 }}>
                  Click "Scan" to request hardware status from ESP32.
                </div>
              )}
            </div>
          </div>

          <EncoderPanel frame={frame} debugReport={debugReport} onScan={scan} onLog={addLog} />
        </div>

        {/* Right column: calibration */}
        <div className="debug-card">
          <div className="debug-card-header">
            <span className="debug-card-title">Sensor Calibration & Motor Tuning</span>
          </div>
          <div className="debug-card-body">
            <div className="cal-list">
              <LoadCal frame={frame} debugReport={debugReport} lastAck={lastAck} onScan={scan} onLog={addLog} />
              {CAL_ITEMS.map(item => (
                <div className="cal-item" key={item.key}>
                  <div className="cal-item-top">
                    <div>
                      <div className="cal-item-name">{item.label}</div>
                      <div className="cal-item-sub">{item.sub}</div>
                    </div>
                    <button className="btn-primary" style={{ padding: "4px 10px", fontSize: 10 }} onClick={() => runCal(item)}>
                      Run
                    </button>
                  </div>

                  {item.hasAxis && (
                    <div style={{ marginTop: 6, display: "flex", gap: 12, flexWrap: "wrap" }}>
                      {([
                        ["boomAxis", "boomInv", "Boom angle (IMU cross-check)"],
                        ["swingAxis", "swingInv", "Boom lean"],
                      ] as const).map(([axisField, invField, label]) => (
                        <div key={axisField}>
                          <div style={{ fontSize: 9, color: "var(--text-secondary)", marginBottom: 3 }}>{label}</div>
                          <div className="cal-axis-row">
                            {(["X", "Y"] as AxisChoice[]).map(a => (
                              <button
                                key={a}
                                className={`axis-badge ${(calState[item.key]?.[axisField] ?? "X") === a ? "sel" : ""}`}
                                onClick={() => setCalAxis(item.key, axisField, a)}
                              >
                                {a}
                              </button>
                            ))}
                            <button
                              className={`axis-badge ${(calState[item.key]?.[invField] ?? false) ? "sel" : ""}`}
                              onClick={() => setCalAxis(item.key, invField, !(calState[item.key]?.[invField] ?? false))}
                              style={{ marginLeft: 4 }}
                            >
                              INV
                            </button>
                          </div>
                        </div>
                      ))}
                    </div>
                  )}

                  {calState[item.key]?.status && (
                    <div className="cal-item-status" style={{ marginTop: 4 }}>← {calState[item.key].status}</div>
                  )}
                </div>
              ))}

              <RulerCal cfg={SWING_RULER} frame={frame} debugReport={debugReport} onScan={scan} onLog={addLog} />
              <RulerCal cfg={TELE_RULER} frame={frame} debugReport={debugReport} onScan={scan} onLog={addLog} />
              <RulerCal cfg={ROPE_RULER} frame={frame} debugReport={debugReport} onScan={scan} onLog={addLog} />
            </div>
          </div>
        </div>
      </div>

      {/* Terminal log bar at bottom */}
      <div className="terminal-bar">
        <div className="terminal-header">
          <div className="terminal-dot" />
          ACK / Command Log
        </div>
        <div className="terminal-body" ref={termRef}>
          {logs.length === 0 && <div className="terminal-line dim-text">Waiting for commands...</div>}
          {logs.map((l, i) => (
            <div key={i} className={`terminal-line ${l.type}`}>{l.text}</div>
          ))}
        </div>
      </div>

      <style>{`@keyframes spin { from{transform:rotate(0deg)} to{transform:rotate(360deg)} }`}</style>
    </div>
  );
}
