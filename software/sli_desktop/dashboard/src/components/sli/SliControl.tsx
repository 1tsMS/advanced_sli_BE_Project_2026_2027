import { useEffect, useRef, useState } from "react";
import { ShieldCheck, ShieldOff, RotateCcw } from "lucide-react";
import {
  allowedMotions, fmtLeft, liveSafety, reasonText, useNow, MOTION_NAME, type ReceivedSafety,
} from "./sliStatus";

const API_BASE = "http://localhost:8000/api";

const BYPASS_OPTIONS = [
  { s: 60,  label: "1 min" },
  { s: 300, label: "5 min" },
  { s: 600, label: "10 min" },
];

async function post(path: string, body?: object): Promise<string | null> {
  try {
    const res = await fetch(`${API_BASE}${path}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: body ? JSON.stringify(body) : undefined,
    });
    if (res.ok) return null;
    const data = await res.json().catch(() => ({}));
    return data.detail ?? "Failed";
  } catch {
    return "Backend offline";
  }
}

interface SliControlProps {
  safety: ReceivedSafety;
}

/**
 * Header SLI switch. The ESP32 enforces; this only shows its state and asks
 * for a timed bypass (with a confirm), a re-arm (no confirm: it is the safe
 * direction) or an E-stop reset.
 */
export function SliControl({ safety }: SliControlProps) {
  const [confirming, setConfirming] = useState(false);
  const [err, setErr] = useState<string | null>(null);
  const wrapRef = useRef<HTMLDivElement>(null);
  const now = useNow();

  // Close the confirm on outside click / Escape
  useEffect(() => {
    if (!confirming) return;
    const onDown = (e: MouseEvent) => {
      if (wrapRef.current && !wrapRef.current.contains(e.target as Node)) setConfirming(false);
    };
    const onKey = (e: KeyboardEvent) => { if (e.key === "Escape") setConfirming(false); };
    document.addEventListener("mousedown", onDown);
    document.addEventListener("keydown", onKey);
    return () => {
      document.removeEventListener("mousedown", onDown);
      document.removeEventListener("keydown", onKey);
    };
  }, [confirming]);

  const s = liveSafety(safety, now);
  const state = s ? s.state : null;
  const mode = s ? s.mode : "ARMED";
  const bypassLeft = s ? s.bypassLeftS : 0;

  const run = async (path: string, body?: object) => {
    setErr(await post(path, body));
    setConfirming(false);
  };

  // When not armed the chip says so: the state is only what the SLI WOULD do
  const chipClass = !state ? "stale" : state === "ESTOP" ? "estop" : mode !== "ARMED" ? "disarmed" : state.toLowerCase();
  const chipText = !state ? "NO SIGNAL" : state === "ESTOP" ? "E-STOP" : mode === "OFF" ? "OFF" : state;

  return (
    <div className="sli-ctl" ref={wrapRef}>
      <div className={`sli-chip ${chipClass}`} title={s
        ? `SLI ${s.state} · ${s.pct.toFixed(1)} % of ${s.capKg.toFixed(3)} kg · chart ${s.chart}`
        : "No SLI status from the ESP32"}>
        <span className="sli-chip-k">SLI</span>
        <span className="sli-chip-v">{chipText}</span>
      </div>

      {state === "ESTOP" ? (
        <button className="sli-btn reset" onClick={() => run("/sli/reset")} title="Clear the latched E-stop (RST)">
          <RotateCcw size={12} /> RESET
        </button>
      ) : mode === "OFF" ? (
        <button className="sli-btn disarmed" onClick={() => run("/sli/arm")} title="Turn the SLI back on">
          <ShieldOff size={12} /> SLI OFF · turn on
        </button>
      ) : mode === "BYPASS" ? (
        <button className="sli-btn disarmed" onClick={() => run("/sli/arm")} title="Re-arm the SLI now">
          <ShieldOff size={12} /> BYPASS <span className="sli-left">{fmtLeft(bypassLeft)}</span>
        </button>
      ) : (
        <button className="sli-btn armed" disabled={!s} aria-expanded={confirming}
                onClick={() => setConfirming(c => !c)}
                title={s ? "SLI armed: click to bypass or turn it off" : "No SLI status from the ESP32"}>
          <ShieldCheck size={12} /> ARMED
        </button>
      )}

      {confirming && (
        <div className="sli-pop" role="dialog" aria-label="Disable the SLI">
          <div className="sli-pop-title">Stop the SLI cutting motion?</div>
          <div className="sli-pop-body">
            Overload and chart limits will no longer stop the motors. E-stop and the 400 ms
            dead-man stay active either way.
          </div>
          <div className="sli-pop-k">Bypass for a while (re-arms by itself)</div>
          <div className="sli-pop-row">
            {BYPASS_OPTIONS.map(o => (
              <button key={o.s} className="sli-pop-opt" onClick={() => run("/sli/bypass", { seconds: o.s })}>
                {o.label}
              </button>
            ))}
          </div>
          <div className="sli-pop-k">Or turn it off (until you turn it on, or the ESP32 reboots)</div>
          <div className="sli-pop-row">
            <button className="sli-pop-opt off" onClick={() => run("/sli/off")}>Turn SLI off</button>
            <button className="btn-ghost" onClick={() => setConfirming(false)}>Cancel</button>
          </div>
        </div>
      )}

      {err && <span className="rec-err" title={err}>{err}</span>}
    </div>
  );
}

/** Strip under the header while the SLI is bypassed or off. */
export function BypassBanner({ safety }: SliControlProps) {
  const s = liveSafety(safety, useNow());
  if (!s || s.mode === "ARMED") return null;
  return (
    <div className="sli-banner" role="status">
      <span className="sli-banner-tag">{s.mode === "OFF" ? "SLI OFF" : "SLI BYPASSED"}</span>
      <span>
        Load limits are not enforced{s.mode === "BYPASS" ? <> · re-arms in <b>{fmtLeft(s.bypassLeftS)}</b></> : " until you turn it on"}.
        {s.state !== "OK" && s.state !== "ESTOP" && <> Right now the SLI would say <b>{s.state}</b>.</>}
      </span>
      <button className="sli-banner-btn" onClick={() => post("/sli/arm")}>Turn on</button>
    </div>
  );
}

interface BlockAlertProps {
  safety: ReceivedSafety;
  block: { text: string; seq: number };
}

const BLOCK_SHOW_MS = 5000;

/**
 * Flashing alert when the ESP32 refuses or cuts a motion: names the motion,
 * the reason in plain words, and what is still allowed. Re-keyed per event so
 * each refusal restarts the flash and the timer.
 */
export function BlockAlert({ safety, block }: BlockAlertProps) {
  if (block.seq === 0) return null;
  return <BlockAlertBody key={block.seq} safety={safety} text={block.text} />;
}

function BlockAlertBody({ safety, text }: { safety: ReceivedSafety; text: string }) {
  const [visible, setVisible] = useState(true);
  const now = useNow();
  useEffect(() => {
    const t = setTimeout(() => setVisible(false), BLOCK_SHOW_MS);
    return () => clearTimeout(t);
  }, []);
  if (!visible) return null;

  // $ACK,SLI,REFUSED|CUT,A<axis>,D<dir>,<reason>
  const p = text.split(",");
  const axis = Number(p[3]?.slice(1)), dir = Number(p[4]?.slice(1)), code = p[5] ?? "";
  const name = MOTION_NAME[axis]?.[dir] ?? `Axis ${axis}`;
  const s = liveSafety(safety, now);
  const allowed = s && s.state !== "ESTOP" ? allowedMotions(s) : [];

  return (
    <div className="sli-block" role="alert">
      <span className="sli-block-tag">{p[2] === "CUT" ? "STOPPED" : "BLOCKED"}</span>
      <span className="sli-block-msg">
        <b>{name}</b> {p[2] === "CUT" ? "was stopped" : "is blocked"}: {reasonText(code, s)}
      </span>
      {allowed.length > 0 && allowed.length < 8 && (
        <span className="sli-block-allowed">Still allowed: {allowed.join(", ")}</span>
      )}
      <button className="sli-block-x" onClick={() => setVisible(false)} aria-label="Dismiss">×</button>
    </div>
  );
}
