import type { TelemetryFrame } from "../../types";

/**
 * Two different things used to share the "ERR (>10kg)" label:
 *  - "loadErr": the load reading itself is impossible (> 10 kg or clearly negative):
 *    HX711 wiring, tare or calibration.
 *  - "outside": the load is fine, but the boom angle / extension is outside the load
 *    chart, so capacity is 0 and the firmware reports 999 % (SLI_PCT_MAX).
 */
export type LoadStatus = "ok" | "loadErr" | "outside";

export function loadStatus(f: TelemetryFrame | null | undefined): LoadStatus {
  const load = f?.actualLoad ?? 0;
  if (load > 10.0 || load < -0.5) return "loadErr";
  if ((f?.loadPercent ?? 0) >= 900) return "outside";
  return "ok";
}
