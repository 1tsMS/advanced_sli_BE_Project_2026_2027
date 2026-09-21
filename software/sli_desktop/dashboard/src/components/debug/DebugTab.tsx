import { useState, useEffect, useRef } from "react";
import { RefreshCw, RotateCcw, Ruler, Save } from "lucide-react";
import type { TelemetryFrame } from "../../types";
import { useHoldToMove } from "../../hooks/useHoldToMove";
import { LoadGainCalibration } from "./LoadGainCalibration";

const API_BASE = "http://localhost:8000/api";

interface DebugEntry { bus: string; address: string | null; status: string; }
interface DebugReport { entries: DebugEntry[]; }

interface DebugTabProps {
  debugReport: DebugReport | null;
  lastAck: string | null;
  frame?: TelemetryFrame | null;
}

// ---- Human-readable device names + GPIO info ----
const DEVICE_INFO: Record<string, { name: string; gpio: string }> = {
  "I2C0_SWING":   { name: "AS5600 Swing Encoder",    gpio: "SDA=21 SCL=22" },
  "I2C0":         { name: "I2C Bus 0 (Wire)",        gpio: "SDA=21 SCL=22" },
  "I2C1_BOOM":    { name: "AS5600 Boom Encoder",     gpio: "SDA=25 SCL=26" },
  "I2C1":         { name: "I2C Bus 1 (Wire1)",       gpio: "SDA=25 SCL=26" },
  "I2C2_TELE":    { name: "AS5600 Telescope Enc.",   gpio: "SDA=32 SCL=33" },
  "I2C2":         { name: "I2C Bus 2 (SoftI2C)",     gpio: "SDA=32 SCL=33" },
  "MPU6050":      { name: "MPU6050 (IMU)",           gpio: "SDA=21 SCL=22" },
  "HX711":        { name: "HX711 (Load Cell)",       gpio: "DT=17 SCK=16"  },
  "FSR1":         { name: "FSR Front-Left",          gpio: "ADC=34" },
  "FSR2":         { name: "FSR Front-Right",         gpio: "ADC=35" },
  "FSR3":         { name: "FSR Rear-Left",           gpio: "ADC=36" },
  "FSR4":         { name: "FSR Rear-Right",          gpio: "ADC=39" },
  "N20_WINCH":    { name: "N20 Winch Encoder",       gpio: "A=18 B=19" },
  "N20_TICKS":    { name: "N20 Winch Ticks",         gpio: "A=18 B=19" },
  "BOOM_XCHECK":  { name: "Boom IMU vs Encoder (diff °)", gpio: "—" },
  "TELE_SCALE":   { name: "Telescope Scale (mm/rev)", gpio: "ESP32 flash" },
  "TELE_INV":     { name: "Telescope Direction Inverted", gpio: "ESP32 flash" },
};

// Fallback values only. Must match TELE_MM_PER_REVOLUTION / TELE_DEFAULT_INVERT in
// firmware config.h. The live values are read back from the ESP32 flash via DBG.
const TELE_DEFAULT_SCALE  = 19.048;
const TELE_DEFAULT_INVERT = true;

function friendlyName(bus: string, address?: string | null): string {
  if (bus === "I2C0") {
    if (address === "0x36") return "AS5600 Swing Encoder";
    if (address === "0x68" || address === "0x69") return "MPU6050 (IMU)";
  }
  return DEVICE_INFO[bus]?.name ?? bus;
}

function gpioNote(bus: string): string {
  return DEVICE_INFO[bus]?.gpio ?? "—";
}

// Calibration options for standard sensors
type AxisChoice = "X" | "Y";
const AXIS_CHOICES: AxisChoice[] = ["X", "Y"];
const CAL_ITEMS = [
  { key: "tare",  label: "Tare Load Cell", sub: "Zero with no load attached", path: "/calibrate/tare", hasAxis: false },
  { key: "imu",   label: "Zero IMU / Gyro", sub: "Put the boom at its zero pose and hold it still. Select which IMU axis is each angle. Also sets the boom encoder reference for the IMU cross-check.", path: "/calibrate/imu", hasAxis: true },
];

interface CalState { status: string; boomAxis: AxisChoice; tiltAxis: AxisChoice; boomInv: boolean; tiltInv: boolean; }

const CAL_STORAGE_KEY = "sli_debug_cal_state_v3";

const DEFAULT_CAL_STATE: Record<string, CalState> = {
  imu: {
    status: "",
    boomAxis: "X",
    tiltAxis: "Y",
    boomInv: false,
    tiltInv: false,
  },
};

function loadSavedCalState(): Record<string, CalState> {
  try {
    const raw = localStorage.getItem(CAL_STORAGE_KEY);
    if (raw) return { ...DEFAULT_CAL_STATE, ...JSON.parse(raw) };
  } catch {}
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
    return { ok: res.ok, msg: data.message ?? (res.ok ? "Done ✓" : "Failed") };
  } catch {
    return { ok: false, msg: "Backend offline" };
  }
}

export function DebugTab({ debugReport, lastAck, frame }: DebugTabProps) {
  const [scanning, setScanning] = useState(false);
  const [logs, setLogs] = useState<{ text: string; type: "ack" | "info" | "err" }[]>([]);
  const [calState, setCalState] = useState<Record<string, CalState>>(loadSavedCalState);
  const termRef = useRef<HTMLDivElement>(null);

  // ---- Telescope Calibration & Tuning State ----
  // The ESP32 flash is the source of truth; these mirror it (synced from the DBG report).
  const [teleScale, setTeleScale] = useState<number>(TELE_DEFAULT_SCALE);
  const [teleInvert, setTeleInvert] = useState<boolean>(TELE_DEFAULT_INVERT);

  const [measuredMm, setMeasuredMm] = useState<string>("50.0");
  const [jogSpeed, setJogSpeed] = useState<number>(1000);
  const [joggingDir, setJoggingDir] = useState<0 | 1 | null>(null);
  const hold = useHoldToMove();
  const [teleStatus, setTeleStatus] = useState<string>("");

  // Ask the ESP32 for a status report on open so the telescope values load from flash
  useEffect(() => {
    apiPost("/debug/scan");
  }, []);

  // Sync telescope calibration from the ESP32 whenever a DBG report arrives
  useEffect(() => {
    if (!debugReport) return;
    const status = (bus: string) => debugReport.entries.find(e => e.bus === bus)?.status;
    const scale = parseFloat(status("TELE_SCALE") ?? "");
    const inv = status("TELE_INV");
    if (Number.isFinite(scale) && scale > 0.1) setTeleScale(scale);
    if (inv !== undefined) setTeleInvert(inv === "1");
  }, [debugReport]);

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
    addLog("→ DBG (I2C scan requested)", "info");
    await apiPost("/debug/scan");
    setTimeout(() => setScanning(false), 2500);
  };

  const runCal = async (item: typeof CAL_ITEMS[0]) => {
    const s = calState[item.key] ?? DEFAULT_CAL_STATE[item.key];
    const body = item.hasAxis ? {
      boomAxis:   s?.boomAxis ?? "X",
      tiltAxis:   s?.tiltAxis ?? "Y",
      boomInvert: s?.boomInv  ?? false,
      tiltInvert: s?.tiltInv  ?? false,
    } : {};

    addLog(`→ ${item.label} (${item.path})`, "info");
    const result = await apiPost(item.path, body);
    addLog(`← ${result.msg}`, result.ok ? "ack" : "err");
    setCalState(prev => {
      const updated = {
        ...prev,
        [item.key]: { ...(prev[item.key] ?? DEFAULT_CAL_STATE[item.key]), status: result.msg },
      };
      try { localStorage.setItem(CAL_STORAGE_KEY, JSON.stringify(updated)); } catch {}
      return updated;
    });
  };

  const setCalAxis = (key: string, field: string, val: string | boolean) => {
    setCalState(prev => {
      const cur = prev[key] ?? DEFAULT_CAL_STATE[key];
      const updated = {
        ...prev,
        [key]: {
          boomAxis: cur?.boomAxis ?? "X",
          tiltAxis: cur?.tiltAxis ?? "Y",
          boomInv: cur?.boomInv ?? false,
          tiltInv: cur?.tiltInv ?? false,
          status: cur?.status ?? "",
          [field]: val,
        },
      };
      try { localStorage.setItem(CAL_STORAGE_KEY, JSON.stringify(updated)); } catch {}
      return updated;
    });
  };

  // ---- Telescope Calibration Handlers ----
  const handleZeroTele = async () => {
    addLog("→ CAL2 (Reset Telescope Position to 0mm)", "info");
    const result = await apiPost("/calibrate/tele", {});
    addLog(`← ${result.msg}`, result.ok ? "ack" : "err");
    setTeleStatus(result.msg);
  };

  const startJogM3 = (dir: 0 | 1) => {
    setJoggingDir(dir);
    hold.start(3, jogSpeed, dir);   // repeats while held (dead-man)
    addLog(`→ M3 S${jogSpeed} D${dir} (${dir === 0 ? "Retract" : "Extend"})`, "info");
  };

  const stopJogM3 = async () => {
    if (joggingDir !== null) {
      setJoggingDir(null);
      await hold.stop(3);
      addLog("→ M0 A3 (Stop Axis 3)", "info");
    }
  };

  const handleAutoCalibrate = async () => {
    const measured = parseFloat(measuredMm);
    if (isNaN(measured) || measured <= 0) {
      addLog("⚠ Enter a valid measured travel distance (> 0 mm)", "err");
      return;
    }
    const liveExt = frame?.extensionMM ?? 0;
    if (Math.abs(liveExt) < 0.5) {
      addLog("⚠ Live extension is ~0mm. Jog the motor outward before measuring travel!", "err");
      return;
    }
    const curScale = teleScale > 0.1 ? teleScale : TELE_DEFAULT_SCALE;
    const revs = Math.abs(liveExt) / curScale;
    if (revs < 0.05) {
      addLog("⚠ Travel distance is too small (<0.05 revs). Jog further for accurate calibration.", "err");
      return;
    }
    const newScale = parseFloat((measured / revs).toFixed(4));
    let newInvert = teleInvert;
    // If extension read negative while user extended outward, flip inversion
    if (liveExt < 0) {
      newInvert = !teleInvert;
    }
    setTeleScale(newScale);
    setTeleInvert(newInvert);

    addLog(`→ Auto-calibrating: Scale=${newScale} mm/rev, Inv=${newInvert ? 1 : 0}`, "info");
    const result = await apiPost("/calibrate/tele", { scale: newScale, invert: newInvert });
    addLog(`← ${result.msg}`, result.ok ? "ack" : "err");
    setTeleStatus(`Calibrated: ${newScale} mm/rev`);
  };

  const handleSaveManual = async () => {
    if (isNaN(teleScale) || teleScale <= 0.1) {
      addLog("⚠ Scale must be greater than 0.1 mm/rev", "err");
      return;
    }

    addLog(`→ Saving to ESP32 Flash: Scale=${teleScale} mm/rev, Inv=${teleInvert ? 1 : 0}`, "info");
    const result = await apiPost("/calibrate/tele", { scale: teleScale, invert: teleInvert });
    addLog(`← ${result.msg}`, result.ok ? "ack" : "err");
    setTeleStatus(result.msg);
  };

  const handleResetDefaults = async () => {
    setTeleScale(TELE_DEFAULT_SCALE);
    setTeleInvert(TELE_DEFAULT_INVERT);
    addLog(`→ Resetting Telescope parameters to default (${TELE_DEFAULT_SCALE} mm/rev, Invert=${TELE_DEFAULT_INVERT ? 1 : 0})`, "info");
    const result = await apiPost("/calibrate/tele", { scale: TELE_DEFAULT_SCALE, invert: TELE_DEFAULT_INVERT });
    addLog(`← ${result.msg}`, result.ok ? "ack" : "err");
    setTeleStatus(`Reset to default ${TELE_DEFAULT_SCALE} mm/rev`);
  };

  const liveExtMM = frame?.extensionMM ?? 0;
  const activeScale = teleScale > 0.1 ? teleScale : TELE_DEFAULT_SCALE;
  const liveRevs = liveExtMM / activeScale;

  return (
    <div className="debug-page">
      {/* Top two columns */}
      <div className="debug-main">

        {/* I2C / Sensor Scan */}
        <div className="debug-card">
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
            {debugReport && debugReport.entries.length > 0 ? (
              <table className="scan-table">
                <thead>
                  <tr>
                    <th>Device</th>
                    <th>GPIO</th>
                    <th>Address</th>
                    <th>Status</th>
                  </tr>
                </thead>
                <tbody>
                  {debugReport.entries.filter(e => e.bus !== "GAIN").map((e, i) => {
                    const isFail = e.status === "FAIL" || e.status === "ERR" || e.status === "NOT FOUND" || e.status === "MISMATCH";
                    const isNumeric = e.status !== "—" && e.status !== "" && !isNaN(Number(e.status));
                    const isOk = e.status === "OK" || (!isFail && isNumeric);
                    return (
                      <tr key={i}>
                        <td>{friendlyName(e.bus, e.address)}</td>
                        <td className="dim-text">{gpioNote(e.bus)}</td>
                        <td className="dim-text">{e.address ?? "—"}</td>
                        <td className={isOk ? "ok-text" : isFail ? "fail-text" : "dim-text"}>
                          {e.status}
                        </td>
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

        {/* Calibration */}
        <div className="debug-card">
          <div className="debug-card-header">
            <span className="debug-card-title">Sensor Calibration & Motor Tuning</span>
          </div>
          <div className="debug-card-body">
            <div className="cal-list">
              {/* Standard Tare and IMU */}
              {CAL_ITEMS.map(item => (
                <div className="cal-item" key={item.key}>
                  <div className="cal-item-top">
                    <div>
                      <div className="cal-item-name">{item.label}</div>
                      <div className="cal-item-sub">{item.sub}</div>
                    </div>
                    <button
                      className="btn-primary"
                      style={{ padding: "4px 10px", fontSize: 10 }}
                      onClick={() => runCal(item)}
                    >
                      Run
                    </button>
                  </div>

                  {/* IMU axis mapping UI */}
                  {item.hasAxis && (
                    <div style={{ marginTop: 6, display: "flex", flexDirection: "column", gap: 5 }}>
                      <div style={{ fontSize: 9, color: "var(--text-muted)", marginBottom: 2 }}>
                        Axis Mapping (select which physical axis is used for each angle):
                      </div>
                      <div style={{ display: "flex", gap: 12, flexWrap: "wrap" }}>
                        {/* Boom angle axis */}
                        <div>
                          <div style={{ fontSize: 9, color: "var(--text-secondary)", marginBottom: 3 }}>Boom Lift</div>
                          <div className="cal-axis-row">
                            {AXIS_CHOICES.map(a => (
                              <button
                                key={a}
                                className={`axis-badge ${(calState[item.key]?.boomAxis ?? "X") === a ? "sel" : ""}`}
                                onClick={() => setCalAxis(item.key, "boomAxis", a)}
                              >
                                {a}
                              </button>
                            ))}
                            <button
                              className={`axis-badge ${(calState[item.key]?.boomInv ?? false) ? "sel" : ""}`}
                              onClick={() => setCalAxis(item.key, "boomInv", !(calState[item.key]?.boomInv ?? false))}
                              style={{ marginLeft: 4 }}
                            >
                              INV
                            </button>
                          </div>
                        </div>
                        {/* Boom lean axis */}
                        <div>
                          <div style={{ fontSize: 9, color: "var(--text-secondary)", marginBottom: 3 }}>Boom Lean</div>
                          <div className="cal-axis-row">
                            {AXIS_CHOICES.map(a => (
                              <button
                                key={a}
                                className={`axis-badge ${(calState[item.key]?.tiltAxis ?? "Y") === a ? "sel" : ""}`}
                                onClick={() => setCalAxis(item.key, "tiltAxis", a)}
                              >
                                {a}
                              </button>
                            ))}
                            <button
                              className={`axis-badge ${(calState[item.key]?.tiltInv ?? false) ? "sel" : ""}`}
                              onClick={() => setCalAxis(item.key, "tiltInv", !(calState[item.key]?.tiltInv ?? false))}
                              style={{ marginLeft: 4 }}
                            >
                              INV
                            </button>
                          </div>
                        </div>
                      </div>
                    </div>
                  )}

                  {calState[item.key]?.status && (
                    <div className="cal-item-status" style={{ marginTop: 4 }}>
                      ← {calState[item.key].status}
                    </div>
                  )}
                </div>
              ))}

              {/* Rich Telescope Calibration & Motor Tuning Section */}
              <div className="cal-item" style={{ border: "1px solid var(--border-accent)" }}>
                <div className="cal-item-top">
                  <div>
                    <div className="cal-item-name" style={{ color: "var(--cyan)", display: "flex", alignItems: "center", gap: 6 }}>
                      <Ruler size={13} />
                      Telescope Extension Calibration & Tuning
                    </div>
                    <div className="cal-item-sub">
                      Interactive M3 motor jog, ruler measurement, and NVS flash calibration
                    </div>
                  </div>
                  <button
                    className="btn-primary"
                    style={{ padding: "4px 10px", fontSize: 10, background: "rgba(0, 212, 255, 0.15)" }}
                    onClick={handleZeroTele}
                    title="Zero encoder at current retracted position"
                  >
                    <RotateCcw size={10} />
                    Set Zero (Retracted)
                  </button>
                </div>

                <div className="tele-cal-box">
                  {/* Live Readout Strip */}
                  <div className="tele-readout-strip">
                    <div className="tele-readout-left">
                      <div>
                        <div style={{ fontSize: 9, color: "var(--text-muted)", textTransform: "uppercase", letterSpacing: 0.5 }}>Live Extension</div>
                        <div className="tele-val-lg" style={{ color: liveExtMM < -0.2 ? "var(--amber)" : "var(--cyan)" }}>
                          {liveExtMM >= 0 ? `+${liveExtMM.toFixed(1)}` : liveExtMM.toFixed(1)} <span style={{ fontSize: 12, fontWeight: 500 }}>mm</span>
                        </div>
                      </div>
                      <div style={{ marginLeft: 16 }}>
                        <div style={{ fontSize: 9, color: "var(--text-muted)", textTransform: "uppercase", letterSpacing: 0.5 }}>Revolutions</div>
                        <div className="tele-val-sub" style={{ fontSize: 13, color: "var(--text-primary)", fontWeight: 600 }}>
                          {liveRevs >= 0 ? `+${liveRevs.toFixed(2)}` : liveRevs.toFixed(2)} <span style={{ color: "var(--text-muted)", fontSize: 10 }}>revs</span>
                        </div>
                      </div>
                    </div>
                    <div style={{ textAlign: "right" }}>
                      <div style={{ fontSize: 9, color: "var(--text-muted)", textTransform: "uppercase", letterSpacing: 0.5 }}>Active Scale</div>
                      <div style={{ fontFamily: "var(--font-mono)", fontSize: 11, color: "var(--text-secondary)" }}>
                        {activeScale.toFixed(3)} mm/rev
                      </div>
                      <div style={{ fontSize: 9.5, color: teleInvert ? "var(--green)" : "var(--amber)", marginTop: 2 }}>
                        {teleInvert ? "● Direction Inverted" : "○ Normal Direction"}
                      </div>
                    </div>
                  </div>

                  {/* Axis 3 Motor Jog Controls */}
                  <div className="tele-section-card">
                    <div className="tele-section-title">
                      <span>Axis 3 (Telescope) Motor Jog</span>
                      <div style={{ display: "flex", gap: 4 }}>
                        {[500, 1000, 2000].map(s => (
                          <button
                            key={s}
                            className={`axis-badge ${jogSpeed === s ? "sel" : ""}`}
                            style={{ padding: "1px 6px", fontSize: 9 }}
                            onClick={() => setJogSpeed(s)}
                          >
                            {s}
                          </button>
                        ))}
                        <span style={{ fontSize: 9, color: "var(--text-muted)", alignSelf: "center", marginLeft: 2 }}>steps/s</span>
                      </div>
                    </div>
                    <div style={{ display: "flex", gap: 6, marginTop: 4 }}>
                      <button
                        className={`tele-jog-btn ${joggingDir === 0 ? "pressed" : ""}`}
                        onMouseDown={() => startJogM3(0)}
                        onMouseUp={stopJogM3}
                        onMouseLeave={stopJogM3}
                        onTouchStart={e => { e.preventDefault(); startJogM3(0); }}
                        onTouchEnd={stopJogM3}
                      >
                        ◄ Retract (M3 D0)
                      </button>
                      <button
                        className={`tele-jog-btn ${joggingDir === 1 ? "pressed" : ""}`}
                        onMouseDown={() => startJogM3(1)}
                        onMouseUp={stopJogM3}
                        onMouseLeave={stopJogM3}
                        onTouchStart={e => { e.preventDefault(); startJogM3(1); }}
                        onTouchEnd={stopJogM3}
                      >
                        Extend ► (M3 D1)
                      </button>
                      <button
                        className="tele-jog-btn stop"
                        onClick={stopJogM3}
                        title="Emergency Stop Motor 3"
                      >
                        ■
                      </button>
                    </div>
                  </div>

                  {/* Ruler Auto-Calibration Tool */}
                  <div className="tele-section-card">
                    <div className="tele-section-title">
                      <span>Ruler Travel Calibration</span>
                      <span style={{ fontSize: 9, color: "var(--text-muted)", textTransform: "none" }}>
                        1. Retract & Set Zero → 2. Jog out → 3. Measure & Calibrate
                      </span>
                    </div>
                    <div className="tele-input-row" style={{ marginTop: 4 }}>
                      <span style={{ fontSize: 11, color: "var(--text-secondary)" }}>Measured Physical Travel:</span>
                      <input
                        type="number"
                        step="0.1"
                        className="tele-input"
                        value={measuredMm}
                        onChange={e => setMeasuredMm(e.target.value)}
                        placeholder="50.0"
                      />
                      <span style={{ fontSize: 11, color: "var(--text-muted)" }}>mm</span>
                      <button
                        className="btn-primary"
                        onClick={handleAutoCalibrate}
                        style={{ marginLeft: "auto", padding: "5px 12px" }}
                        title="Calculate exact mm/rev from measured travel and update ESP32 flash"
                      >
                        ⚡ Calculate & Save Scale
                      </button>
                    </div>
                  </div>

                  {/* Manual Tuning & Flash Save */}
                  <div className="tele-section-card">
                    <div className="tele-section-title">
                      <span>Manual Fine-Tuning & Persistence</span>
                      <button
                        className="btn-ghost"
                        onClick={handleResetDefaults}
                        style={{ padding: "1px 6px", fontSize: 9 }}
                      >
                        Reset Defaults
                      </button>
                    </div>
                    <div className="tele-input-row" style={{ marginTop: 4 }}>
                      <span style={{ fontSize: 11, color: "var(--text-secondary)" }}>Scale:</span>
                      <input
                        type="number"
                        step="0.001"
                        className="tele-input"
                        value={teleScale}
                        onChange={e => setTeleScale(parseFloat(e.target.value) || 0)}
                      />
                      <span style={{ fontSize: 11, color: "var(--text-muted)" }}>mm/rev</span>

                      <button
                        className={`axis-badge ${teleInvert ? "sel" : ""}`}
                        onClick={() => setTeleInvert(!teleInvert)}
                        style={{ marginLeft: 6 }}
                      >
                        Invert Dir: {teleInvert ? "YES" : "NO"}
                      </button>

                      <button
                        className="btn-primary"
                        onClick={handleSaveManual}
                        style={{ marginLeft: "auto", padding: "5px 12px" }}
                        title="Save scale and inversion directly to ESP32 NVS flash"
                      >
                        <Save size={10} />
                        Save to Flash
                      </button>
                    </div>
                  </div>

                  {teleStatus && (
                    <div className="cal-item-status" style={{ marginTop: 2, color: "var(--cyan)" }}>
                      ← {teleStatus}
                    </div>
                  )}
                </div>
              </div>

              {/* Boom-angle correction for the load cell */}
              <LoadGainCalibration frame={frame ?? null} debugReport={debugReport} lastAck={lastAck} />
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
          {logs.length === 0 && (
            <div className="terminal-line dim-text">Waiting for commands...</div>
          )}
          {logs.map((l, i) => (
            <div key={i} className={`terminal-line ${l.type}`}>
              {l.text}
            </div>
          ))}
        </div>
      </div>

      <style>{`@keyframes spin { from{transform:rotate(0deg)} to{transform:rotate(360deg)} }`}</style>
    </div>
  );
}
