import type { TelemetryFrame } from "../../types";
import { Compass, Scale, ShieldAlert, ArrowUpRight } from "lucide-react";
import { STATUS, TONE_COLOR, hasStatus, loadLabel, loadTone } from "../../status";

interface TelemetryPanelProps {
  frame: TelemetryFrame | null;
}

const fmt = (v: number, d = 2) => (Number.isFinite(v) ? v.toFixed(d) : "--");

type Tag = "ok" | "warn" | "danger";

/** One row of the system status list. All states come from the firmware's flags. */
function StatusRow({ name, tag, text }: { name: string; tag: Tag; text: string }) {
  return (
    <div className="tel-status-item">
      <span className="status-item-name">{name}</span>
      <span className={`status-item-tag ${tag}`}>{text}</span>
    </div>
  );
}

export function TelemetryPanel({ frame: f }: TelemetryPanelProps) {
  const actualLoad = f?.actualLoad ?? 0;
  const safeLimit = f?.safeLoadLimit ?? 0;
  const pct = f?.loadPercent ?? 0;
  const boomAngle = f?.boomAngle ?? 0;
  const extMM = f?.extensionMM ?? 0;
  const ropeLen = f?.ropeLength ?? 0;
  const swingAngle = f?.swingAngle ?? 0;
  const lean = f?.boomLean ?? 0;

  // Operating radius calculated from real base boom (22.5cm = 0.225m) + telescope extension
  const nominalBoomM = (225 + Math.max(0, extMM)) / 1000;
  const operatingRadiusM = nominalBoomM * Math.cos((boomAngle * Math.PI) / 180);

  // What to show is decided by the firmware (alarm level + status flags)
  const tone = loadTone(f);
  const statusColor = TONE_COLOR[tone];
  const statusLabel = loadLabel(f);
  const loadFault = hasStatus(f, STATUS.LOAD_FAULT);

  const leanWarn = hasStatus(f, STATUS.LEAN_WARN);

  return (
    <div className="panel tel-panel">
      <div className="panel-header">
        <span className="ph-icon">◈</span>
        <span>Crane Telemetry</span>
        <span className="ph-badge" style={{ borderColor: statusColor, color: statusColor }}>
          {statusLabel}
        </span>
      </div>

      <div className="panel-body tel-body">
        {/* HERO LOAD CARD */}
        <div className="tel-hero-card">
          <div className="hero-card-label">
            <Scale size={13} color="var(--cyan)" />
            <span>LOAD & CAPACITY</span>
          </div>

          <div className="hero-load-display">
            {loadFault ? (
              <>
                <span className="hero-load-value" style={{ color: "var(--amber)", fontSize: "1.4rem" }}>
                  FAULT
                </span>
                <span className="hero-load-unit" style={{ color: "var(--text-muted)", fontSize: "0.75rem" }}>
                  [Measured: {fmt(f?.measuredLoad ?? 0, 2)} kg]
                </span>
              </>
            ) : (
              <>
                <span className="hero-load-value" style={{ color: statusColor }}>
                  {fmt(actualLoad, 2)}
                </span>
                <span className="hero-load-unit">kg</span>
              </>
            )}
            <span className="hero-load-limit">/ {fmt(safeLimit, 2)} kg MAX</span>
          </div>

          {/* Large Capacity Progress Bar */}
          <div className="hero-progress-track">
            <div
              className="hero-progress-fill"
              style={{
                width: loadFault ? "100%" : `${Math.min(100, Math.max(0, pct))}%`,
                background: statusColor,
                opacity: loadFault ? 0.4 : 1,
              }}
            />
            <div className="hero-progress-marker warn" style={{ left: "80%" }} />
            <div className="hero-progress-marker danger" style={{ left: "100%" }} />
          </div>

          <div className="hero-progress-meta">
            <span className="hero-pct-label" style={{ color: statusColor }}>
              {loadFault ? "LOAD SENSOR FAULT" : `${fmt(pct, 1)}% SWL`}
            </span>
            <span className="hero-raw-sub">
              {loadFault ? "Check load cell wiring / tare in Debug" : `Measured: ${fmt(f?.measuredLoad ?? 0, 2)} kg`}
            </span>
          </div>
        </div>

        {/* SECTION: BOOM & GEOMETRY */}
        <div className="tel-group">
          <div className="tel-group-header">
            <ArrowUpRight size={13} />
            <span>BOOM GEOMETRY</span>
          </div>

          <div className="tel-card-grid">
            <div className="tel-stat-card">
              <span className="stat-label">Angle</span>
              <span className="stat-value highlight">
                {fmt(boomAngle, 1)}<span className="stat-unit">°</span>
              </span>
            </div>

            <div className="tel-stat-card">
              <span className="stat-label">Radius</span>
              <span className="stat-value">
                {fmt(operatingRadiusM, 2)}<span className="stat-unit">m</span>
              </span>
            </div>

            <div className="tel-stat-card">
              <span className="stat-label">Extension</span>
              <span className="stat-value">
                {fmt(extMM, 0)}<span className="stat-unit">mm</span>
              </span>
            </div>

            <div className="tel-stat-card">
              <span className="stat-label">Rope Length</span>
              <span className="stat-value">
                {fmt(ropeLen, 0)}<span className="stat-unit">mm</span>
              </span>
            </div>
          </div>
        </div>

        {/* SECTION: ORIENTATION */}
        <div className="tel-group">
          <div className="tel-group-header">
            <Compass size={13} />
            <span>ORIENTATION</span>
          </div>

          <div className="tel-card-grid">
            <div className="tel-stat-card">
              <span className="stat-label">Boom Lean</span>
              <span className={`stat-value ${leanWarn ? "danger" : ""}`}>
                {fmt(lean, 1)}<span className="stat-unit">°</span>
              </span>
            </div>

            <div className="tel-stat-card">
              <span className="stat-label">Slew / Swing</span>
              <span className="stat-value">
                {fmt(swingAngle, 1)}<span className="stat-unit">°</span>
              </span>
            </div>
          </div>
        </div>

        {/* SECTION: SYSTEM STATUS — every row is a firmware flag, nothing is derived here */}
        <div className="tel-group">
          <div className="tel-group-header">
            <ShieldAlert size={13} />
            <span>SYSTEM STATUS</span>
          </div>

          <div className="tel-status-list">
            <StatusRow
              name="Load sensor"
              tag={hasStatus(f, STATUS.LOAD_FAULT) ? "danger" : "ok"}
              text={hasStatus(f, STATUS.LOAD_FAULT) ? "FAULT" : "OK"}
            />
            <StatusRow
              name="Boom angle sensor"
              tag={hasStatus(f, STATUS.BOOM_FAULT | STATUS.BOOM_MISMATCH) ? "danger" : "ok"}
              text={
                hasStatus(f, STATUS.BOOM_FAULT) ? "FAULT"
                : hasStatus(f, STATUS.BOOM_MISMATCH) ? "IMU ≠ ENCODER"
                : "OK"
              }
            />
            <StatusRow
              name="Extension sensor"
              tag={hasStatus(f, STATUS.EXT_FAULT) ? "danger" : "ok"}
              text={hasStatus(f, STATUS.EXT_FAULT) ? "FAULT" : "OK"}
            />
            <StatusRow
              name="Load chart"
              tag={hasStatus(f, STATUS.OUT_OF_CHART) ? "danger" : hasStatus(f, STATUS.NO_CHART) ? "warn" : "ok"}
              text={
                hasStatus(f, STATUS.OUT_OF_CHART) ? "OUTSIDE CHART"
                : hasStatus(f, STATUS.NO_CHART) ? "NONE (DEFAULT LIMIT)"
                : "IN CHART"
              }
            />
            <StatusRow
              name="Boom lean"
              tag={leanWarn ? "warn" : "ok"}
              text={leanWarn ? "LEANING" : "OK"}
            />
            <StatusRow
              name="E-stop"
              tag={hasStatus(f, STATUS.ESTOP) ? "danger" : "ok"}
              text={hasStatus(f, STATUS.ESTOP) ? "LATCHED" : "CLEAR"}
            />
          </div>
        </div>

      </div>
    </div>
  );
}
