import { useEffect, useRef, useState } from "react";
import type { SafetyStatus } from "../../types";

/** $S arrives at 5 Hz; older than this means the ESP32 (or its SafetyTask) went quiet. */
export const SLI_STALE_MS = 2000;

export type ReceivedSafety = (SafetyStatus & { receivedAt: number }) | null;

/** The status if it is fresh at time `now`, else null. */
export function liveSafety(s: ReceivedSafety, now: number): SafetyStatus | null {
  return s && now - s.receivedAt < SLI_STALE_MS ? s : null;
}

/** Motion names by axis (1-4) and direction (0/1); same D mapping as MotorControls / firmware. */
export const MOTION_NAME: Record<number, [string, string]> = {
  1: ["Swing ◄", "Swing ►"],
  2: ["Boom down", "Boom up"],
  3: ["Retract", "Extend"],
  4: ["Wind up", "Pay out"],
};

/** Is axis/dir cut by the SLI right now? Only when armed: bypass / OFF enforce nothing. */
export function isBlocked(s: SafetyStatus | null, axis: number, dir: number): boolean {
  if (!s) return false;
  if (s.state === "ESTOP") return true;
  return s.mode === "ARMED" && (s.blocked & (1 << ((axis - 1) * 2 + dir))) !== 0;
}

/**
 * The blocked mask as the BUTTONS should show it: a bit only flips after it has
 * held its new value for `ms`. At a chart edge, sensor noise of +-0.1 mm / 0.1°
 * flips a direction in and out of "blocked" every status line; the ESP32 still
 * decides on every press, this only stops the button flashing red / normal.
 */
export function useSteadyMask(mask: number, ms = 600): number {
  const shown = useRef(mask);
  const since = useRef<Record<number, number>>({});
  const [, force] = useState(0);
  useEffect(() => {
    const now = Date.now();
    let next = shown.current;
    for (let b = 0; b < 8; b++) {
      const bit = 1 << b;
      if ((mask & bit) === (shown.current & bit)) { delete since.current[b]; continue; }
      since.current[b] ??= now;
      if (now - since.current[b] >= ms) { next ^= bit; delete since.current[b]; }
    }
    if (next !== shown.current) { shown.current = next; force(x => x + 1); }
  });
  // re-check while a change is pending
  useEffect(() => {
    const t = setInterval(() => force(x => x + 1), 200);
    return () => clearInterval(t);
  }, []);
  return shown.current;
}

export function allowedMotions(s: SafetyStatus): string[] {
  const out: string[] = [];
  for (const a of [1, 2, 3, 4]) for (const d of [0, 1]) if (!isBlocked(s, a, d)) out.push(MOTION_NAME[a][d]);
  return out;
}

/** Plain-language reason for a block, from the firmware reason code + live status. */
export function reasonText(code: string, s: SafetyStatus | null): string {
  switch (code) {
    case "ESTOP": return "E-stop is latched. Press RESET.";
    case "FAULT": {
      const why = s?.why ?? "-";
      if (s?.chart === "NONE" || why === "NO_CHART")
        return "No load chart on the crane. Upload one in the Load Chart tab, or turn the SLI off.";
      if (why === "SENSOR:LOAD")
        return "Load cell: no reading or a broken wire (stuck at the limit, frozen, or far below zero). Check Calibrate, Load cell.";
      if (why.startsWith("SENSOR:"))
        return `Sensor down: ${why.slice(7).replace(/\+/g, ", ")}. The load moment is unknown until it is back (check Debug).`;
      if (why === "LOAD_RANGE") return "The load reading is impossible (> 10 kg): HX711 wiring or calibration.";
      if (why === "BAD_VALUE")  return "An angle or extension reading is invalid (NaN).";
      return "A sensor is down or giving bad data (check Debug), so the load moment is unknown.";
    }
    case "CUT":
      if (s?.why === "OUTSIDE") return "The crane is outside the load chart (boom angle or extension): only moves back into it are allowed.";
      return s ? `Overload: ${s.pct.toFixed(0)} % of ${s.capKg.toFixed(3)} kg rated here.` : "Overload.";
    case "LIMIT": return "That move would cross the chart's cut line or leave the chart.";
    case "TRAVEL": return "The boom is on its lower stop (IMU at or below -0.5°): boom down is cut while the SLI is armed (bypass or off lifts it).";
    default:      return code;
  }
}

export function fmtLeft(s: number): string {
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, "0")}`;
}

/** Wall clock as state, ticking every `ms`: lets a silent ESP32 turn "stale" on time. */
export function useNow(ms = 1000): number {
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    const t = setInterval(() => setNow(Date.now()), ms);
    return () => clearInterval(t);
  }, [ms]);
  return now;
}
