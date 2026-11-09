/**
 * Crane geometry used for drawing and the radius readout (measured by MS, 2026-11-09).
 * The SLI itself never uses these: the load chart is angle x extension.
 *
 *   pivot (boom bearing centre) ── 225 mm ── boom end ── 63 mm head ── load-cell edge (rope / hook)
 *   = 288 mm with the telescope retracted;  + telescope extension (0 = fully retracted)
 * Same 288 mm as ROPE_FIXED_PATH_MM in the firmware config.h.
 */
export const BOOM_BASE_MM = 225;     // bearing centre to the end of the boom / telescope tube
export const BOOM_HEAD_MM = 63;      // new head (load cell, IMU + HX711 board): boom end to the load-cell edge
export const ROPE_KNOT_TO_HOOK_MM = 76;   // knot to the hook curve (firmware config.h)
export const TELE_RETRACTED_MM = 0;  // telescope reading when fully retracted

/** Pivot to the rope / hook point (mm) for a telescope extension. */
export const hookArmMm = (extMm: number) => BOOM_BASE_MM + BOOM_HEAD_MM + Math.max(0, extMm);
