import type { TelemetryFrame } from "../../types";

/** ESP32 12-bit ADC: 4095 counts = 3.3 V at the pin (FSR resistance ~0). */
const ADC_FULL = 4095;
/** Below this the pad carries nothing (or the FSR is unplugged). */
const PAD_EMPTY = 50;

const PADS: { idx: number; label: string; name: string }[] = [
  { idx: 0, label: "FL", name: "Front left (FSR1)" },
  { idx: 1, label: "FR", name: "Front right (FSR2)" },
  { idx: 2, label: "RL", name: "Rear left (FSR3)" },
  { idx: 3, label: "RR", name: "Rear right (FSR4)" },
];

function level(val: number): "off" | "low" | "mid" | "high" {
  if (val < PAD_EMPTY) return "off";
  const p = val / ADC_FULL;
  if (p < 0.3) return "low";
  if (p < 0.65) return "mid";
  return "high";
}

function Pad({ frame, idx, label, name }: { frame: TelemetryFrame | null; idx: number; label: string; name: string }) {
  const counts = frame?.fsr[idx] ?? 0;
  const share = frame?.fsrShare?.[idx];
  const kg = frame?.padKg?.[idx];
  const pct = Math.round((counts / ADC_FULL) * 100);
  const tip = `${name}: ${counts} of ${ADC_FULL} counts (${pct} % of full scale)`
    + (share !== undefined ? `, ${Math.round(share * 100)} % of the total pad force` : "");
  const loaded = counts >= PAD_EMPTY;
  return (
    <div className="fsr-dot-wrap" style={{ justifySelf: "center" }} title={tip}>
      <div className="fsr-dot" data-level={level(counts)}>
        {!loaded ? "—" : kg !== undefined ? <span>{(kg * 1000).toFixed(0)}<small> g</small></span> : `${pct}%`}
      </div>
      <div className="fsr-dot-label">
        {label}{share !== undefined && loaded ? ` · ${Math.round(share * 100)}%` : ""}
      </div>
    </div>
  );
}

/** Outrigger pads in their physical layout, tipping margin and grounding check. */
export function FsrPanel({ frame }: { frame: TelemetryFrame | null }) {
  const fsr = frame?.fsr ?? [0, 0, 0, 0];
  const grounded = fsr.filter(v => v >= PAD_EMPTY).length;
  return (
    <div className="panel fsr-panel">
      <div className="panel-header">Outrigger pads</div>
      <div className="fsr-body">
        <div className="fsr-layout">
          <Pad frame={frame} {...PADS[0]} />
          <div />
          <Pad frame={frame} {...PADS[1]} />
          <div />
          <div className="fsr-center-label">BASE</div>
          <div />
          <Pad frame={frame} {...PADS[2]} />
          <div />
          <Pad frame={frame} {...PADS[3]} />
        </div>

        <div className="fsr-caption">
          {frame?.padKg
            ? "Dot ≈ weight on the pad (share × crane + hook load). Label = share of the total."
            : <>Dot = % of ADC full scale (100 % = {ADC_FULL} counts = 3.3 V). Set the crane weight in Settings to show ≈ grams per pad.</>}
        </div>

        {frame?.tipValid && frame.tipMargin !== null && frame.tipMargin !== undefined && (
          <div className={`tip-readout ${frame.tipWarn ? "warn" : ""}`}
               title="Centre of pressure distance to the nearest pad edge (100 % centred, 0 % = about to tip)">
            {frame.tipWarn ? "⚠ " : ""}Tipping margin <b>{frame.tipMargin.toFixed(0)} %</b>
          </div>
        )}

        {grounded > 0 && (
          <span className={`state-chip ${grounded === 4 ? "ok" : "warn"}`}>
            {grounded === 4 ? "All 4 pads loaded" : `△ ${4 - grounded} pad${grounded === 3 ? "" : "s"} unloaded`}
          </span>
        )}
      </div>
    </div>
  );
}
