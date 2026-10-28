import type { LoadChartProfile } from "../../types";

// Mirrors firmware load_chart.h / safety_logic.h. The ESP32 is the authority;
// these only drive the previews, headroom and validation in the editor.
export const LC_MAX_ANGLES = 8;
export const LC_MAX_EXTS = 6;
export const LC_MAX_ZONES = 4;
export const LC_TOL_DEG = 1.0;
export const LC_TOL_MM = 2.0;
export const MIN_CAP_KG = 0.001;
export const MAX_CAP_KG = 10;
export const NAME_RE = /^[A-Za-z0-9_ .-]{1,15}$/;

/** Segment index + fraction of x in increasing breakpoints, or null outside (beyond tol). */
function locate(b: number[], x: number, tol: number): [number, number] | null {
  const n = b.length;
  if (!(x >= b[0] - tol && x <= b[n - 1] + tol)) return null;
  if (x <= b[0]) return [0, 0];
  if (x >= b[n - 1]) return [n - 2, 1];
  for (let i = 0; i + 1 < n; i++) {
    if (x <= b[i + 1]) return [i, (x - b[i]) / (b[i + 1] - b[i])];
  }
  return null;
}

/** Swing zone factor and index (-1 = none) for any swing angle. */
export function zoneFactor(c: LoadChartProfile, swingDeg: number): [number, number] {
  let s = swingDeg % 360;
  if (s < 0) s += 360;
  for (let z = 0; z < c.zones.length; z++) {
    const { fromDeg: f, toDeg: t, factor } = c.zones[z];
    const inside = f < t ? s >= f && s < t : s >= f || s < t;
    if (inside) return [factor, z];
  }
  return [1, -1];
}

/** Rated capacity (kg) at a pose; 0 outside the chart. Same maths as lc_capacity(). */
export function capacityAt(c: LoadChartProfile, boomDeg: number, extMm: number, swingDeg: number): number {
  const a = locate(c.anglesDeg, boomDeg, LC_TOL_DEG);
  const e = locate(c.extensionsMM, extMm, LC_TOL_MM);
  if (!a || !e) return 0;
  const [ia, ta] = a;
  const [ie, te] = e;
  const k = c.capacityKg;
  const lo = k[ia][ie] + (k[ia][ie + 1] - k[ia][ie]) * te;
  const hi = k[ia + 1][ie] + (k[ia + 1][ie + 1] - k[ia + 1][ie]) * te;
  return (lo + (hi - lo) * ta) * zoneFactor(c, swingDeg)[0];
}

export interface Headroom {
  boomDownDeg: number | null;   // how far it can go before the cut (null = already cut / unknown)
  extendMm: number | null;
}

/**
 * How far boom-down and extend can go from this pose before the load % reaches
 * the cut threshold (or the chart edge). Marches in small steps, like the
 * firmware's look-ahead but all the way to the limit.
 */
export function headroom(c: LoadChartProfile, boomDeg: number, extMm: number, swingDeg: number, loadKg: number): Headroom {
  const pctAt = (b: number, e: number) => {
    const cap = capacityAt(c, b, e, swingDeg);
    return cap > 0 ? (Math.max(0, loadKg) / cap) * 100 : Infinity;
  };
  if (pctAt(boomDeg, extMm) >= c.cutPct) return { boomDownDeg: null, extendMm: null };
  const march = (step: number, limit: number, at: (d: number) => number) => {
    let d = 0;
    while (d + step <= limit && at(d + step) < c.cutPct) d += step;
    return d;
  };
  return {
    boomDownDeg: march(0.1, 180, d => pctAt(boomDeg - d, extMm)),
    extendMm: march(0.2, 500, d => pctAt(boomDeg, extMm + d)),
  };
}

/** Same rules as the backend / firmware: null = OK, else the first problem. */
export function validateProfile(p: LoadChartProfile): string | null {
  if (!NAME_RE.test(p.name) || p.name !== p.name.trim())
    return "Name: 1–15 letters, digits, space, . _ - (no leading/trailing space)";
  const a = p.anglesDeg, e = p.extensionsMM;
  if (a.length < 2 || a.length > LC_MAX_ANGLES) return `Need 2–${LC_MAX_ANGLES} boom angles`;
  if (e.length < 2 || e.length > LC_MAX_EXTS) return `Need 2–${LC_MAX_EXTS} extensions`;
  if (![...a, ...e].every(Number.isFinite)) return "Every angle and extension needs a number";
  for (let i = 0; i + 1 < a.length; i++) if (!(a[i + 1] > a[i])) return "Boom angles must increase down the table";
  for (let i = 0; i + 1 < e.length; i++) if (!(e[i + 1] > e[i])) return "Extensions must increase left to right";
  if (a[0] < -90 || a[a.length - 1] > 90) return "Boom angles must be within −90…90°";
  for (const row of p.capacityKg)
    for (const v of row)
      if (!(Number.isFinite(v) && v >= MIN_CAP_KG && v <= MAX_CAP_KG))
        return `Every capacity must be ${MIN_CAP_KG}–${MAX_CAP_KG} kg`;
  if (p.zones.length > LC_MAX_ZONES) return `At most ${LC_MAX_ZONES} swing zones`;
  for (const z of p.zones) {
    if (!(z.fromDeg >= 0 && z.fromDeg < 360 && z.toDeg >= 0 && z.toDeg <= 360) || z.fromDeg === z.toDeg)
      return "Zone angles must be 0–360 and not equal";
    if (!(z.factor > 0 && z.factor <= 1.5)) return "Zone factor must be above 0 and at most 1.5";
  }
  if (!(p.cutPct > 0 && p.cutPct <= 150)) return "Cut % must be above 0 and at most 150";
  if (!(p.warnPct > 0 && p.warnPct < p.cutPct)) return "Warn % must be above 0 and below cut %";
  return null;
}

/** Derived profiles: the base is assumed to hold MEASURED limits (tipping / stall). */
export const DERIVE_PRESETS = [
  { key: "MAX",  label: "Max capacity", factor: 1.0,  hint: "100 % of the measured limits" },
  { key: "STD",  label: "Standard",     factor: 0.85, hint: "85 % (ASME B30.5, outriggers set)" },
  { key: "SAFE", label: "Max safety",   factor: 0.65, hint: "65 %, for demos and unsupervised runs" },
] as const;

export function deriveProfile(base: LoadChartProfile, key: string, factor: number): LoadChartProfile {
  const suffix = ` ${key}`;
  const stem = base.name.replace(/ (MAX|STD|SAFE)$/, "").slice(0, 15 - suffix.length).trimEnd();
  return {
    ...base,
    name: stem + suffix,
    description: `${Math.round(factor * 100)} % of ${base.name}`,
    capacityKg: base.capacityKg.map(r => r.map(v => Math.max(MIN_CAP_KG, Math.round(v * factor * 1000) / 1000))),
    zones: base.zones.map(z => ({ ...z })),
    updated: null,
  };
}

/** Same chart content (ignores description / updated): used for "on the crane" matching. */
export function sameChart(a: LoadChartProfile, b: LoadChartProfile): boolean {
  const near = (x: number, y: number) => Math.abs(x - y) < 0.0015;
  const arr = (x: number[], y: number[]) => x.length === y.length && x.every((v, i) => near(v, y[i]));
  return a.name === b.name && arr(a.anglesDeg, b.anglesDeg) && arr(a.extensionsMM, b.extensionsMM) &&
    a.capacityKg.length === b.capacityKg.length && a.capacityKg.every((r, i) => arr(r, b.capacityKg[i])) &&
    a.zones.length === b.zones.length &&
    a.zones.every((z, i) => near(z.fromDeg, b.zones[i].fromDeg) && near(z.toDeg, b.zones[i].toDeg) && near(z.factor, b.zones[i].factor)) &&
    near(a.warnPct, b.warnPct) && near(a.cutPct, b.cutPct);
}

/** Sequential capacity ramp (one hue, dark = less), validated on --bg-card #121620. */
export const CAP_RAMP = ["#1F5A6B", "#1A7186", "#168AA3", "#22A3BF", "#4DBCD4", "#86D5E7"];

export function rampColor(v: number, min: number, max: number): string {
  if (!(max > min)) return CAP_RAMP[CAP_RAMP.length - 1];
  const t = Math.min(1, Math.max(0, (v - min) / (max - min)));
  return CAP_RAMP[Math.min(CAP_RAMP.length - 1, Math.floor(t * CAP_RAMP.length))];
}
