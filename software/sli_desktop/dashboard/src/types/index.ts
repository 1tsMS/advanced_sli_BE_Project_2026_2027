export interface TelemetryFrame {
  boomAngle: number;
  extensionMM: number;
  measuredLoad: number;
  loadCellRaw?: number;
  actualLoad: number;
  swingAngle: number;
  ropeLength: number;   // calibrated: load-cell end to the knot
  ropeTotal: number;    // ropeLength + fixed path + knot-to-hook (0 = old firmware)
  hookDrop: number;     // load-cell end to hook tip (0 = old firmware)
  fsr: [number, number, number, number];
  imuRoll: number;
  imuPitch: number;
  safeLoadLimit: number;
  loadPercent: number;
  alarmLevel: 0 | 1 | 2 | 3; // 0=OK, 1=WARN, 2=CRITICAL, 3=ESTOP
  timestamp: number;
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

/** CSV recording state from the backend (off until the REC button is pressed). */
export interface RecordingState {
  recording: boolean;
  file: string | null;
  rows: number;
  elapsedSec: number;
}

/** $S from the ESP32: the SLI state (5 Hz + on every change). */
export type SliState = "OK" | "WARN" | "CUT" | "FAULT" | "ESTOP";
export interface SafetyStatus {
  state: SliState;
  pct: number;
  capKg: number;
  zone: number;          // -1 = none
  bypassLeftS: number;   // 0 = armed
  blocked: number;       // bit (axis-1)*2 + dir set = that motion is cut
  chart: string;         // active chart name on the ESP32, "NONE" if none
  mode: "ARMED" | "BYPASS" | "OFF";   // state + blocked are enforced only when ARMED
  timestamp: number;     // 0 = nothing received yet
}

export interface LoadZone {
  fromDeg: number;
  toDeg: number;
  factor: number;
}

/** Load chart profile (backend JSON + ESP32 upload). Rows follow anglesDeg, columns extensionsMM. */
export interface LoadChartProfile {
  name: string;
  description: string;
  anglesDeg: number[];
  extensionsMM: number[];
  capacityKg: number[][];
  zones: LoadZone[];
  warnPct: number;
  cutPct: number;
  updated?: string | null;
}

export type TabId =
  | "dashboard"
  | "debug"
  | "datalogger"
  | "loadchart"
  | "settings";
