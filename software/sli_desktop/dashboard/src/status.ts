import type { TelemetryFrame } from "./types";

/**
 * Status flag bits sent by the firmware in `statusFlags` (see STATUS_* in
 * firmware config.h — keep the two in step). The dashboard only displays
 * them; every safety decision is made on the ESP32.
 */
export const STATUS = {
  LOAD_FAULT:   1 << 0, // load cell stale, or reading outside its valid range
  BOOM_FAULT:   1 << 1, // boom-angle IMU not responding: boom angle unreliable
  EXT_FAULT:    1 << 2, // telescope encoder not responding: extension unreliable
  NO_CHART:     1 << 3, // no load chart saved: default limit in use
  OUT_OF_CHART: 1 << 4, // position outside the load chart: limit is 0 kg
  LEAN_WARN:    1 << 5, // boom leaning sideways beyond the warning angle
  ESTOP:        1 << 6, // E-stop latched
  BOOM_MISMATCH: 1 << 7, // boom IMU and boom encoder disagree
} as const;

export const hasStatus = (
  f: Pick<TelemetryFrame, "statusFlags"> | null | undefined,
  flag: number,
): boolean => !!f && (f.statusFlags & flag) !== 0;

export type LoadTone = "ok" | "warn" | "overload" | "fault" | "estop";

/** How the load should be presented, decided from the firmware's alarm level and flags. */
export function loadTone(f: TelemetryFrame | null): LoadTone {
  if (!f) return "ok";
  if (f.alarmLevel === 3 || hasStatus(f, STATUS.ESTOP)) return "estop";
  if (hasStatus(f, STATUS.LOAD_FAULT | STATUS.BOOM_FAULT | STATUS.EXT_FAULT | STATUS.BOOM_MISMATCH)) return "fault";
  if (f.alarmLevel === 2) return "overload";
  if (f.alarmLevel === 1) return "warn";
  return "ok";
}

export function loadLabel(f: TelemetryFrame | null): string {
  switch (loadTone(f)) {
    case "estop":    return "E-STOP";
    case "overload": return "OVERLOAD";
    case "warn":     return "WARNING";
    case "fault":
      return hasStatus(f, STATUS.LOAD_FAULT) ? "LOAD SENSOR FAULT"
        : hasStatus(f, STATUS.BOOM_MISMATCH) && !hasStatus(f, STATUS.BOOM_FAULT | STATUS.EXT_FAULT)
          ? "BOOM ANGLE MISMATCH"
          : "POSITION SENSOR FAULT";
    default:         return "NORMAL";
  }
}

/** CSS colour for each tone. */
export const TONE_COLOR: Record<LoadTone, string> = {
  ok:       "var(--green)",
  warn:     "var(--amber)",
  overload: "var(--red)",
  fault:    "var(--amber)",
  estop:    "var(--red)",
};
