import { useCallback, useState } from "react";
import {
  LayoutDashboard, SlidersHorizontal, LineChart, ClipboardCheck, Grid3x3, Settings, OctagonX,
} from "lucide-react";
import type { TabId } from "./types";
import { useTelemetry } from "./hooks/useTelemetry";
import { TelemetryPanel } from "./components/telemetry/TelemetryPanel";
import { FsrPanel } from "./components/telemetry/FsrPanel";
import { MotorControls } from "./components/controls/MotorControls";
import { CraneVisualizer } from "./components/visualizer/CraneVisualizer";
import { Gauge } from "./components/shared/Gauge";
import { loadStatus } from "./components/telemetry/loadStatus";
import { SettingsTab } from "./components/settings/SettingsTab";
import { DebugTab } from "./components/debug/DebugTab";
import { SliControl, BypassBanner, BlockAlert } from "./components/sli/SliControl";
import { liveSafety, useNow } from "./components/sli/sliStatus";
import { LoadChartTab } from "./components/loadchart/LoadChartTab";
import { DataLoggerTab } from "./components/datalogger/DataLoggerTab";
import { OperatorTab } from "./components/operator/OperatorTab";
import { LiftControl } from "./components/controls/LiftControl";
import { API_BASE } from "./api";

/** Navigation in display-hierarchy order: L1 overview, L3 detail, L4 diagnostics, setup. */
const NAV: { id: TabId; icon: React.ReactNode; label: string }[] = [
  { id: "dashboard",  icon: <LayoutDashboard size={20} />,  label: "Crane" },
  { id: "datalogger", icon: <LineChart size={20} />,        label: "Data" },
  { id: "operator",   icon: <ClipboardCheck size={20} />,   label: "Report" },
  { id: "loadchart",  icon: <Grid3x3 size={20} />,          label: "Chart" },
  { id: "debug",      icon: <SlidersHorizontal size={20} />, label: "Calibrate" },
  { id: "settings",   icon: <Settings size={20} />,         label: "Settings" },
];

const ALARM_LABEL = ["ONLINE", "WARNING", "CRITICAL", "E-STOP"];
const ALARM_CLASS = ["", "warn", "critical", "estop"];

async function estop() {
  try { await fetch(`${API_BASE}/estop`, { method: "POST" }); } catch { /* offline */ }
}

export default function App() {
  const [tab, setTab]     = useState<TabId>("dashboard");
  const [speed, setSpeed] = useState(1000);

  const { frame, debugReport, lastAck, wsStatus, espConnected,
          safety, craneChart, lcResult, sliBlock, history, capture, startCapture, stopCapture, shaping } = useTelemetry();
  const [selectedLift, setSelectedLift] = useState<string | null>(null);
  const [liftsVersion, setLiftsVersion] = useState(0);
  // A finished lift opens straight in the Data Logger review
  const onLiftStopped = useCallback((id: string) => {
    setSelectedLift(id);
    setLiftsVersion(v => v + 1);
    setTab("datalogger");
  }, []);

  // Old firmware without $S: fall back to the alarm level in the telemetry
  const sliLive = !!liveSafety(safety, useNow()) && wsStatus === "connected";
  const alarm = frame?.alarmLevel ?? 0;
  const warnPct = craneChart?.chart?.warnPct;
  const cutPct = craneChart?.chart?.cutPct;

  return (
    <div className="app-shell">
      <aside className="sidebar">
        <div className="sidebar-brand" title="Advanced Safe Load Indicator">SLI</div>
        {NAV.map(n => (
          <button key={n.id} className={`nav-btn ${tab === n.id ? "active" : ""}`}
                  onClick={() => setTab(n.id)} aria-current={tab === n.id ? "page" : undefined}>
            {n.icon}
            <span className="nav-label">{n.label}</span>
          </button>
        ))}
        <div className="sidebar-spacer" />
        <div className="sidebar-footer">
          <div className={`sidebar-conn ${wsStatus === "connected" ? "live" : ""}`}
               title={wsStatus === "connected" ? "Backend: connected" : "Backend: not connected"}>
            <span className="conn-tag">SRV</span>
          </div>
          <div className={`sidebar-conn ${espConnected ? "live" : ""}`}
               title={espConnected ? "ESP32: connected" : "ESP32: not connected"}>
            <span className="conn-tag">ESP</span>
          </div>
        </div>
      </aside>

      <div className="main-area">
        <header className="header">
          <div className="header-title">
            <b>Advanced SLI</b>
            <span>Scale-model crane · safe load indicator</span>
          </div>
          <div className="header-fill" />

          {!sliLive && (
            <div className={`alarm-pill ${wsStatus === "connected" ? ALARM_CLASS[alarm] : ""}`}>
              {wsStatus === "connected" ? ALARM_LABEL[alarm] : wsStatus === "connecting" ? "CONNECTING" : "OFFLINE"}
            </div>
          )}
          <SliControl safety={safety} />
          <div className="header-sep" />
          <LiftControl onStopped={onLiftStopped} />
          <button className="estop-btn" id="estop-button" onClick={estop}>
            <OctagonX size={18} /> E-STOP
          </button>
        </header>

        <BypassBanner safety={safety} />
        <BlockAlert safety={safety} block={sliBlock} />

        <main className="page">
          {tab === "dashboard" && (
            <div className="dash-grid">
              <TelemetryPanel frame={frame} safety={safety} chart={craneChart?.chart} warnPct={warnPct} cutPct={cutPct} />

              <div className="center-col">
                <div className="panel crane-panel">
                  <div className="panel-header">Crane view</div>
                  <div className="crane-canvas-wrap">
                    <CraneVisualizer frame={frame} />
                  </div>
                  <div className="gauges-row">
                    <Gauge label="Load" unit="% of rated" value={frame?.loadPercent ?? NaN} max={120}
                           warnAt={warnPct ?? 90} critAt={cutPct ?? 100}
                           text={loadStatus(frame) === "outside" ? ["OUT", "of chart"] : loadStatus(frame) === "loadErr" ? ["ERR", "load"] : null} />
                    <Gauge label="Boom angle" unit="°" value={frame?.boomAngle ?? NaN} max={60} />
                    <Gauge label="Tilt" unit="° lean" value={frame?.imuPitch ?? NaN} min={-10} max={10} warnAt={3} critAt={5} />
                  </div>
                </div>
                <MotorControls speed={speed} onSpeedChange={setSpeed} safety={safety} shaping={shaping} />
              </div>

              <div className="right-col">
                <FsrPanel frame={frame} />
              </div>
            </div>
          )}

          {tab === "debug" && <DebugTab debugReport={debugReport} lastAck={lastAck} frame={frame} />}
          {tab === "settings" && <SettingsTab wsConnected={wsStatus === "connected"} safety={safety} />}
          {tab === "loadchart" && (
            <LoadChartTab frame={frame} safety={safety} craneChart={craneChart} lcResult={lcResult} />
          )}
          {tab === "datalogger" && (
            <DataLoggerTab history={history} capture={capture} onStartCapture={startCapture} onStopCapture={stopCapture}
                           selectedLift={selectedLift} onSelectLift={setSelectedLift}
                           liftsVersion={liftsVersion} warnPct={warnPct} cutPct={cutPct} />
          )}
          {tab === "operator" && (
            <OperatorTab selectedLift={selectedLift} onSelectLift={setSelectedLift} liftsVersion={liftsVersion} />
          )}
        </main>
      </div>
    </div>
  );
}
