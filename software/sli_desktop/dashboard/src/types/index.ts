export interface TelemetryFrame {
  boomAngle: number;
  extensionMM: number;
  /** Uncorrected load cell reading in kg */
  measuredLoad: number;
  /** Load in kg after the boom-angle correction */
  actualLoad: number;
  swingAngle: number;
  ropeLength: number;
  fsr: [number, number, number, number];
  /** Sideways lean of the boom in degrees (IMU on the boom) */
  boomLean: number;
  /** Bit field, see STATUS in ../status.ts */
  statusFlags: number;
  safeLoadLimit: number;
  loadPercent: number;
  alarmLevel: 0 | 1 | 2 | 3; // 0=OK, 1=WARN, 2=CRITICAL, 3=ESTOP
  timestamp: number;
}

/** One telemetry sample kept for the rolling charts (t = seconds, client clock). */
export interface HistoryPoint {
  t: number;
  boomAngle: number;
  extensionMM: number;
  actualLoad: number;
  safeLoadLimit: number;
  loadPercent: number;
}

export interface LoadChartRow {
  angle: number;
  extensionMM: number;
  limitKg: number;
}

export interface DebugEntry {
  bus: string;
  address: string | null;
  status: string;
}

export interface DebugReport {
  entries: DebugEntry[];
}

export type AlarmLevel = 0 | 1 | 2 | 3;

export type TabId =
  | "dashboard"
  | "debug"
  | "datalogger"
  | "loadchart"
  | "settings";
