import { useState, useEffect } from "react";
import { RefreshCw, ChevronUp, ChevronDown } from "lucide-react";
import type { TelemetryFrame, DebugReport } from "../../types";
import { EncoderPanel } from "./EncoderPanel";
import { RulerCal } from "./RulerCal";
import { LoadCal } from "./LoadCal";
import { CalSection, CalSteps, type CalStatus } from "./CalSection";
import { TELE_RULER, ROPE_RULER, SWING_RULER, type RulerAxisConfig } from "./rulerAxes";
import { isEncoderDetailLine } from "./encoderDiag";
import { LOAD_CELL_FAULTS, loadCellVerdict } from "./loadCellHealth";
import { API_BASE } from "../../api";

interface DebugTabProps {
  debugReport: DebugReport | null;
  lastAck: string | null;
  frame?: TelemetryFrame | null;
}

// ---- Human-readable device names + where they are wired ----
const DEVICE_INFO: Record<string, { name: string; where: string }> = {
  "I2C0_SWING":   { name: "Swing encoder (AS5600)",  where: "SDA 32 · SCL 33" },
  "I2C1_BOOM":    { name: "Boom encoder (AS5600)",   where: "SDA 25 · SCL 26" },
  "I2C2_TELE":    { name: "Telescope encoder",       where: "SDA 15 · SCL 5" },
  "MPU6050":      { name: "IMU (MPU6050)",           where: "SDA 21 · SCL 22" },
  "HX711":        { name: "Load cell (HX711)",       where: "DT 16 · SCK 17" },
  "HX711_RAW":    { name: "Load cell raw counts",    where: "DT 16 · SCK 17" },
  "LOAD_CAL":     { name: "Load cell scale",         where: "flash" },
  "LOAD_CELL":    { name: "Load cell bridge (wires)", where: "behind the HX711" },
  "FSR1":         { name: "Pad front left",          where: "ADC 34" },
  "FSR2":         { name: "Pad front right",         where: "ADC 35" },
  "FSR3":         { name: "Pad rear left",           where: "ADC 39" },
  "FSR4":         { name: "Pad rear right",          where: "ADC 36" },
  "N20_TICKS":    { name: "Winch encoder ticks",     where: "A 18 · B 19" },
  "SENSOR_LOOP":  { name: "Sensor loop timing",      where: "core 1" },
  "LOOP_STAGES":  { name: "Slowest loop stage",      where: "core 1" },
  "IMU_DROPOUTS": { name: "IMU dropouts / bad samples", where: "bus 0" },
  "STACKS":       { name: "Task stack free (bytes)", where: "RTOS" },
  "SERIAL_TX":    { name: "Serial lines dropped",    where: "RTOS" },
  "NVS":          { name: "Flash writes",            where: "RTOS" },
  "RESET":        { name: "Last reset / free heap",  where: "RTOS" },
};
const BAD = ["FAIL", "ERR", "NOT FOUND", "SLOW", "NOISY", "LOW", "DROPS", ...Object.keys(LOAD_CELL_FAULTS)];

type AxisChoice = "X" | "Y";
interface ImuChoice { boomAxis: AxisChoice; tiltAxis: AxisChoice; boomInv: boolean; tiltInv: boolean; last: string; }
const IMU_KEY = "sli_imu_cal_choice";
const IMU_DEFAULT: ImuChoice = { boomAxis: "X", tiltAxis: "Y", boomInv: false, tiltInv: false, last: "" };
const OPEN_KEY = "sli_cal_open";

function loadJson<T>(key: string, fallback: T): T {
  try { const v = localStorage.getItem(key); if (v) return { ...fallback, ...JSON.parse(v) }; } catch { /* blocked */ }
  return fallback;
}

async function apiPost(path: string, body: object = {}) {
  try {
    const res = await fetch(`${API_BASE}${path}`, {
      method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body),
    });
    const data = await res.json();
    return { ok: res.ok, msg: data.message ?? data.detail ?? (res.ok ? "Done" : "Failed") };
  } catch {
    return { ok: false, msg: "Backend offline" };
  }
}

/** Status of a ruler-calibrated axis from its AXIS_<tag> line. */
function axisStatus(report: DebugReport | null, cfg: RulerAxisConfig): CalStatus {
  const line = report?.entries.find(e => e.bus === `AXIS_${cfg.diagTag}`);
  if (!line) return { text: "Scan", level: "off" };
  const scale = /scale:([-\d.]+)/.exec(line.address ?? "")?.[1];
  const isDefault = scale !== undefined && Math.abs(parseFloat(scale) - cfg.defaultScale) < 1e-6;
  return isDefault ? { text: "Default", level: "warn" } : { text: "Stored", level: "ok" };
}

export function DebugTab({ debugReport, lastAck, frame }: DebugTabProps) {
  const [scanning, setScanning] = useState(false);
  const [logs, setLogs] = useState<{ text: string; type: "ack" | "info" | "err" }[]>([]);
  const [logOpen, setLogOpen] = useState(false);
  const [imu, setImu] = useState<ImuChoice>(() => loadJson(IMU_KEY, IMU_DEFAULT));
  const [open, setOpen] = useState<string>(() => { try { return localStorage.getItem(OPEN_KEY) ?? "load"; } catch { return "load"; } });

  // Read diagnostics + stored calibration as soon as the tab opens
  useEffect(() => { apiPost("/debug/scan"); }, []);

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

  const toggle = (key: string) => {
    const next = open === key ? "" : key;      // one section open at a time
    setOpen(next);
    try { localStorage.setItem(OPEN_KEY, next); } catch { /* not remembered */ }
  };

  const setImuField = <K extends keyof ImuChoice>(k: K, v: ImuChoice[K]) => {
    setImu(prev => {
      const next = { ...prev, [k]: v };
      try { localStorage.setItem(IMU_KEY, JSON.stringify(next)); } catch { /* not remembered */ }
      return next;
    });
  };

  const zeroImu = async () => {
    addLog("→ Zero IMU (CAL1)", "info");
    const r = await apiPost("/calibrate/imu", {
      boomAxis: imu.boomAxis, tiltAxis: imu.tiltAxis, boomInvert: imu.boomInv, tiltInvert: imu.tiltInv,
    });
    addLog(`← ${r.msg}`, r.ok ? "ack" : "err");
    setImuField("last", r.ok ? new Date().toLocaleString() : "");
  };

  const health = debugReport?.entries.filter(e => !isEncoderDetailLine(e.bus)) ?? [];
  const loadCal = debugReport?.entries.find(e => e.bus === "LOAD_CAL");
  const cellVerdict = loadCellVerdict(debugReport);
  const loadStatus: CalStatus = cellVerdict && LOAD_CELL_FAULTS[cellVerdict] ? { text: "Cell fault", level: "warn" }
    : !loadCal ? { text: "Scan", level: "off" }
    : loadCal.status === "OK" ? { text: "Calibrated", level: "ok" } : { text: "Default scale", level: "warn" };
  const imuStatus: CalStatus = imu.last ? { text: "Zeroed", level: "ok" } : { text: "Run at start", level: "warn" };
  const problems = health.filter(e => BAD.includes(e.status)).length;

  const imuAxisPicker = (axisK: "boomAxis" | "tiltAxis", invK: "boomInv" | "tiltInv", label: string) => (
    <div className="imu-pick">
      <span className="imu-pick-k">{label}</span>
      <div className="seg">
        {(["X", "Y"] as AxisChoice[]).map(a => (
          <button key={a} className={imu[axisK] === a ? "on" : ""} onClick={() => setImuField(axisK, a)}>{a}</button>
        ))}
      </div>
      <label className="check"><input type="checkbox" checked={imu[invK]} onChange={e => setImuField(invK, e.target.checked)} /> Invert</label>
    </div>
  );

  return (
    <div className="debug-page">
      <div className="debug-main">
        {/* ---------- left: what the ESP32 reports ---------- */}
        <div className="debug-col">
          <div className="panel debug-health">
            <div className="panel-header">
              System health
              <span className="ph-right">
                {debugReport && (
                  <span className={`state-chip ${problems ? "warn" : "ok"}`}>
                    {problems ? `△ ${problems} to check` : "All OK"}
                  </span>
                )}
                <button className="btn-ghost btn-sm" onClick={scan} disabled={scanning}>
                  <RefreshCw size={14} className={scanning ? "spin" : ""} /> {scanning ? "Scanning" : "Scan"}
                </button>
              </span>
            </div>
            <div className="panel-body">
              {health.length > 0 ? (
                <table className="health-table">
                  <tbody>
                    {health.map((e, i) => {
                      const bad = BAD.includes(e.status);
                      const isNum = e.status !== "" && !isNaN(Number(e.status));
                      return (
                        <tr key={i}>
                          <td>
                            <div className="h-name">{DEVICE_INFO[e.bus]?.name ?? e.bus}</div>
                            <div className="h-where">{DEVICE_INFO[e.bus]?.where ?? ""}{e.address ? ` · ${e.address}` : ""}</div>
                          </td>
                          <td className="h-status">
                            {isNum ? <span className="h-num">{e.status}</span>
                              : <span className={`state-chip ${bad ? "warn" : e.status === "OK" ? "ok" : "off"}`}>{bad ? "△ " : ""}{e.status}</span>}
                          </td>
                        </tr>
                      );
                    })}
                  </tbody>
                </table>
              ) : <div className="empty-note">Press Scan to read the sensor status from the ESP32.</div>}
            </div>
          </div>

          <EncoderPanel frame={frame} debugReport={debugReport} onScan={scan} onLog={addLog} />
        </div>

        {/* ---------- right: calibration, in the order to do it ---------- */}
        <div className="panel debug-cal">
          <div className="panel-header">
            Calibration
            <span className="ph-right"><span className="ph-hint">in this order · each step saves to the ESP32</span></span>
          </div>
          <div className="panel-body cal-list">
            <CalSection n={1} title="Load cell" summary="Tare + 2-3 known weights" status={loadStatus}
                        open={open === "load"} onToggle={() => toggle("load")}>
              <LoadCal frame={frame} debugReport={debugReport} lastAck={lastAck} onScan={scan} onLog={addLog} />
            </CalSection>

            <CalSection n={2} title="IMU zero" summary="Boom still at its zero pose" status={imuStatus}
                        open={open === "imu"} onToggle={() => toggle("imu")}>
              <div className="cal-body">
                <CalSteps
                  steps={["Lower the boom to its zero pose and keep the crane still.",
                          "Pick which IMU axis gives the boom angle and the lean.",
                          "Zero IMU: the gyro and accelerometer are averaged for ~1 s."]}
                  action={<button className="btn-primary" onClick={zeroImu}>Zero IMU</button>}
                />
                <div className="imu-picks">
                  {imuAxisPicker("boomAxis", "boomInv", "Boom angle axis")}
                  {imuAxisPicker("tiltAxis", "tiltInv", "Boom lean axis")}
                </div>
                {imu.last && <div className="cal-note">Last zeroed {imu.last}</div>}
              </div>
            </CalSection>

            {[TELE_RULER, SWING_RULER, ROPE_RULER].map((cfg, i) => (
              <CalSection key={cfg.key} n={3 + i} title={cfg.title} summary={cfg.summary}
                          status={axisStatus(debugReport, cfg)} open={open === cfg.key} onToggle={() => toggle(cfg.key)}>
                <RulerCal cfg={cfg} frame={frame} debugReport={debugReport} onScan={scan} onLog={addLog} />
              </CalSection>
            ))}
          </div>
        </div>
      </div>

      {/* ---------- command log: a drawer, closed by default ---------- */}
      <div className={`log-drawer ${logOpen ? "open" : ""}`}>
        <button className="log-drawer-head" onClick={() => setLogOpen(o => !o)} aria-expanded={logOpen}>
          {logOpen ? <ChevronDown size={16} /> : <ChevronUp size={16} />}
          <span className="log-drawer-title">Command log</span>
          {!logOpen && <span className={`log-last ${logs[0]?.type ?? ""}`}>{logs[0]?.text ?? "No commands yet"}</span>}
          <span className="log-count">{logs.length}</span>
        </button>
        {logOpen && (
          <div className="log-body">
            {logs.length === 0 && <div className="log-line">No commands yet.</div>}
            {logs.map((l, i) => <div key={i} className={`log-line ${l.type}`}>{l.text}</div>)}
          </div>
        )}
      </div>
    </div>
  );
}
