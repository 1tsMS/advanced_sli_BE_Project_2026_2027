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
  fsr: [number, number, number, number];   // ADC counts, 0-4095 = 0-3.3 V (smoothed on the ESP32)
  imuRoll: number;
  imuPitch: number;
  safeLoadLimit: number;
  loadPercent: number;
  alarmLevel: 0 | 1 | 2 | 3; // 0=OK, 1=WARN, 2=CRITICAL, 3=ESTOP
  // Boom-tip IMU, peaks since the previous frame (0 with firmware before 2026-11-07)
  accelDyn?: number;   // m/s² beyond gravity
  jerk?: number;       // m/s³
  // Dashboard-side (useTelemetry, components/telemetry/shock.ts): idle noise removed
  accelShock?: number; // m/s² above the idle level, 0 at rest
  jerkShock?: number;  // m/s³ above the idle level
  accelPeak?: number;  // accelShock, peak of the last 2 s (readable number for the tile)
  imuRejected?: number;
  timestamp: number;
  // Tipping margin from the FSRs (backend, warn only)
  tipValid?: boolean;
  tipMargin?: number | null;   // % (100 = centred, 0 = on the pad edge)
  copX?: number | null;
  copY?: number | null;
  tipWarn?: boolean;
  fsrShare?: number[] | null;   // each pad's share of the total pad force (0-1)
  padKg?: number[] | null;      // approx kg on each pad (null until the crane weight is set)
}

/** One live sample kept for the rolling charts (Data Logger, last 60 s). */
export interface LivePoint {
  t: number;          // s, relative to the newest sample when drawn
  ts: number;         // unix s
  load: number;
  cap: number;
  pct: number | null;   // null outside the chart (the ESP32 reports 999 %)
  boom: number;
  swing: number;
  ext: number;
  hookDrop: number;
  tip: number | null;
  accel: number;      // m/s² above the idle level (shock filter, 0 at rest)
  jerk: number;       // m/s³ above the idle level
}

/** 5-minute capture for the live charts: points with t = seconds since start. */
export interface Capture {
  active: boolean;
  t0: number;          // unix s at start
  points: LivePoint[];
}

/** Swing input shaping state ($SHP from the ESP32). */
export interface ShapingState {
  on: boolean;
  lengthMm: number;
  halfPeriodMs: number;
}

export interface LiftState {
  active: boolean;
  id: string | null;
  operator: string | null;
  elapsedS: number;
  rows: number;
  shaping: boolean;
}

export interface LiftSummary {
  id: string;
  name: string;              // set with Rename in the Data tab ("" = not named yet)
  operator: string | null;
  note: string;
  startedAt: string | null;
  durationS: number | null;
  liftedKg: number | null;
  score: number | null;
  grade: string | null;
  ok: boolean;
}

/** One recorded row (lift CSV). m1..m4 = commanded direction per axis (-1/0/+1). */
export interface LiftRow {
  t: number;
  load: number;
  pct: number | null;
  capKg: number | null;
  state: string;
  mode: string;
  boom: number | null;
  ext: number | null;
  swing: number | null;
  hookDrop: number | null;
  tipValid: number | null;
  tipMargin: number | null;
  m1: number; m2: number; m3: number; m4: number;
  shaping: number | null;
  accelDyn?: number | null;
  jerk?: number | null;
}

export interface LiftPhase { name: string; t0: number; t1: number; }
export interface LiftFlag { t: number; level: "info" | "warn" | "crit"; text: string; }

/** Analysis report (backend core/lift_analysis.py). */
export interface LiftReport {
  ok: boolean;
  reason?: string;
  id: string;
  operator: string;
  note: string;
  startedAt: string;
  durationS: number;
  samples: number;
  rateHz: number;
  load: { liftedKg: number; peakKg: number; ratedKg: number | null; meanPct: number | null; peakPct: number | null };
  pickups: { t: number; staticKg: number; peakKg: number; daf: number; snatch: boolean; heldS: number }[];
  phases: LiftPhase[];
  phaseTotals: Record<string, number>;
  safety: { warnEntries: number; cutEntries: number; warnS: number; cutS: number; tipWarnS: number;
            minTipMargin: number | null; modes: string[]; refusals: number; estops: number };
  smoothness: { dafMax: number; jogs: number; jogsPerMin: number; reversals: number; reversalsPerMin: number;
                meanRippleG: number; swingStopRippleG: number | null;
                stops: { t: number; axis: string; rippleG: number; shaped: boolean }[] };
  efficiency: { idleS: number; idleFrac: number };
  dynamics?: { peakAccel: number | null; peakJerk: number | null;
               shocks: { t: number; accel: number; jerk: number; cause: string }[] };
  penalty: { points: number; items: { what: string; value: number; points: number }[] };
  score: { total: number; grade: string; safety: number; smoothness: number; efficiency: number;
           max: { safety: number; smoothness: number; efficiency: number };
           parts: Record<string, number> };
  flags: LiftFlag[];
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
  why?: string;          // - | NO_CHART | SENSOR:IMU+LOAD.. | LOAD_RANGE | BAD_VALUE | OUTSIDE
  cutMode?: "P" | "R" | "S";   // predictive / reactive / speed-aware (test E5)
  drvIdle?: boolean | null;    // Mega releases idle drivers (null = older firmware)
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
  | "operator"
  | "loadchart"
  | "settings";
