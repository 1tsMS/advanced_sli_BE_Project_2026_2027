import type { TelemetryFrame } from "../../types";
import type { MotionAxis } from "../../hooks/useHoldToMove";

const API_BASE = "http://localhost:8000/api";

/** What differs between the telescope (AS5600 turns) and the rope (N20 counts). */
export interface RulerAxisConfig {
  key: "tele" | "rope" | "swing";
  unit: string;                    // what the ruler / protractor measures: "mm" or "°"
  minSteps: number;                // smallest encoder movement accepted for a point
  title: string;
  sub: string;
  zeroLabel: string;
  zeroHint: string;
  diagTag: string;                 // AXIS_<tag> line in the DBG report
  stepName: string;                // what the encoder counts: "turns" / "counts"
  stepDigits: number;
  scaleUnit: string;               // "mm/rev" / "mm/count"
  scaleDigits: number;
  defaultScale: number;
  defaultInvert: boolean;
  motorAxis: MotionAxis;
  outDir: 0 | 1;                   // M-command direction that makes the value grow
  outLabel: string;
  inLabel: string;
  value: (f: TelemetryFrame | null | undefined) => number;
  /** What the encoder itself measures, if it differs from `value` (rope: the drum side,
   *  without the telescope compensation that `value` includes). Steps are counted from this. */
  stepValue?: (f: TelemetryFrame | null | undefined) => number;
  /** Fit measured = offset + scale * steps instead of a line through the zero. For axes
   *  whose zero position doesn't read 0 on the tape (rope: hook fully up is not 0 mm). */
  fitOffset?: boolean;
  zero: () => Promise<{ ok: boolean; msg: string }>;
  /** value = what the axis measures right now; sent as its new reference (fitOffset axes). */
  save: (scale: number, invert: boolean, value?: number) => Promise<{ ok: boolean; msg: string }>;
}

// config.h ROPE_FIXED_PATH_MM + ROPE_KNOT_TO_HOOK_MM: ropeTotal = drum-side rope + this
const ROPE_TOTAL_EXTRA_MM = 245 + 90;

async function apiPost(path: string, body: object = {}) {
  try {
    const res = await fetch(`${API_BASE}${path}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
    const data = await res.json().catch(() => ({}));
    return { ok: res.ok, msg: data.message ?? data.detail ?? (res.ok ? "Done" : "Failed") };
  } catch {
    return { ok: false, msg: "Backend offline" };
  }
}

export const TELE_RULER: RulerAxisConfig = {
  key: "tele",
  unit: "mm",
  minSteps: 0.05,
  title: "Telescope Extension (2-point)",
  sub: "Point 1: fully retract → Set Zero. Point 2: extend, measure with a ruler, Record point → Save fit. More points = better fit.",
  zeroLabel: "Set Zero (Retracted)",
  zeroHint: "Fully retract the telescope, then press: this position becomes 0 mm",
  diagTag: "TELE",
  stepName: "turns", stepDigits: 3,
  scaleUnit: "mm/rev", scaleDigits: 4,
  defaultScale: 19.048, defaultInvert: true,   // config.h
  motorAxis: 3, outDir: 1, outLabel: "Extend ►", inLabel: "◄ Retract",
  value: f => f?.extensionMM ?? 0,
  zero: () => apiPost("/calibrate/tele", {}),
  save: (scale, invert) => apiPost("/calibrate/tele", { scale, invert }),
};

export const ROPE_RULER: RulerAxisConfig = {
  key: "rope",
  unit: "mm",
  minSteps: 50,
  title: "Rope Length (2-point, winch)",
  sub: "Measure load-cell end → knot with a tape. Record a point at the top, pay out, record 2+ more → Save fit. The fit doesn't need the zero to be exact.",
  zeroLabel: "Set Zero (Hook Up)",
  zeroHint: "Wind the hook fully up, then press: this rope length becomes 0 mm",
  diagTag: "ROPE",
  stepName: "counts", stepDigits: 0,
  scaleUnit: "mm/count", scaleDigits: 6,
  defaultScale: (Math.PI * 20) / 3360, defaultInvert: false,   // config.h guess
  motorAxis: 4, outDir: 1, outLabel: "Pay out ▼", inLabel: "▲ Wind up",
  value: f => f?.ropeLength ?? 0,
  // ropeLength subtracts the rope the telescope pulled over the tip; the encoder doesn't see that
  stepValue: f => (f && f.ropeTotal > 0 ? f.ropeTotal - ROPE_TOTAL_EXTRA_MM : f?.ropeLength ?? 0),
  fitOffset: true,
  zero: () => apiPost("/calibrate/encoder", { axis: "rope", value: 0 }),
  save: (scale, invert, value) => apiPost("/calibrate/rope", { scale, invert, value }),
};

export const SWING_RULER: RulerAxisConfig = {
  key: "swing",
  unit: "°",
  minSteps: 0.05,
  title: "Swing Angle (2-point)",
  sub: "Point 1: turntable at home → Set Zero. Point 2: turn to a marked angle (e.g. 90° with a protractor), enter it, Record point → Save fit.",
  zeroLabel: "Set Zero (Home)",
  zeroHint: "Put the turntable at its home position, then press: this becomes 0°",
  diagTag: "SWING",
  stepName: "motor turns", stepDigits: 3,
  scaleUnit: "°/motor rev", scaleDigits: 4,
  defaultScale: 18, defaultInvert: false,   // 20:1 cycloidal: 360 / 20
  motorAxis: 1, outDir: 1, outLabel: "Swing ►", inLabel: "◄ Swing",
  value: f => f?.swingAngle ?? 0,
  zero: () => apiPost("/calibrate/encoder", { axis: "swing", value: 0 }),
  save: (scale, invert) => apiPost("/calibrate/axis-scale", { axis: "swing", scale, invert }),
};
