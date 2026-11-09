/**
 * Display filter for boom-tip acceleration / jerk: shows SHOCKS (a load lifting
 * off, a boom stopping), not the idle noise.
 *
 *   baseline = median of the last WINDOW samples      (what "standing still" reads)
 *   noise    = 3 x median absolute deviation, >= floor (how much it wobbles at rest)
 *   shown    = max(0, raw - baseline - noise)
 *
 * At rest that is 0; a shock above the idle wobble keeps its full height above 0.
 * A median baseline also removes any steady offset (sensor scale error, vibration
 * from a running stepper), and one spike can't move a median. The raw values stay
 * in the recordings; this is only what the dashboard draws.
 */
export class ShockFilter {
  private buf: number[] = [];
  private readonly window: number;
  private readonly floor: number;

  constructor(window = 250, floor = 0.05) {   // 250 samples = 5 s at 50 Hz
    this.window = window;
    this.floor = floor;
  }

  push(raw: number): number {
    if (!Number.isFinite(raw)) return 0;
    this.buf.push(raw);
    if (this.buf.length > this.window) this.buf.shift();
    if (this.buf.length < 10) return 0;            // not enough to know the idle level yet
    const sorted = [...this.buf].sort((a, b) => a - b);
    const median = sorted[sorted.length >> 1];
    const dev = this.buf.map(v => Math.abs(v - median)).sort((a, b) => a - b);
    const noise = Math.max(this.floor, 3 * dev[dev.length >> 1]);
    return Math.max(0, raw - median - noise);
  }
}

/** Peak of the last `ms` of values, for a number that should hold a shock long enough to read. */
export class PeakHold {
  private items: { t: number; v: number }[] = [];
  private readonly ms: number;
  constructor(ms = 2000) { this.ms = ms; }
  push(v: number, t: number): number {
    this.items.push({ t, v });
    while (this.items.length && t - this.items[0].t > this.ms) this.items.shift();
    return this.items.reduce((m, i) => Math.max(m, i.v), 0);
  }
}

/** Tick values every `step` across a stableDomain() range (no 0.15-style ticks). */
export function ticksFor([lo, hi]: [number, number], step: number): number[] {
  const n = Math.round((hi - lo) / step);
  const every = Math.max(1, Math.ceil(n / 5));          // at most ~6 ticks
  const out: number[] = [];
  for (let i = 0; i <= n; i += every) out.push(+(lo + i * step).toFixed(6));
  return out;
}

/**
 * Y-axis range that only changes in whole steps and never shrinks below
 * `minSpan`, so a 0.1° wobble doesn't rescale the chart every redraw.
 */
export function stableDomain(values: (number | null | undefined)[], minSpan: number, step: number,
                             floorZero = false): [number, number] {
  const v = values.filter((x): x is number => typeof x === "number" && Number.isFinite(x));
  if (!v.length) return [floorZero ? 0 : -minSpan / 2, minSpan / 2 * (floorZero ? 2 : 1)];
  let lo = Math.floor(Math.min(...v) / step) * step;
  let hi = Math.ceil(Math.max(...v) / step) * step;
  if (floorZero) lo = Math.min(0, lo);
  if (hi - lo < minSpan) {
    const mid = (hi + lo) / 2;
    lo = floorZero && lo >= 0 ? 0 : Math.floor((mid - minSpan / 2) / step) * step;
    hi = lo + Math.ceil(minSpan / step) * step;
  }
  return [lo, hi];
}
