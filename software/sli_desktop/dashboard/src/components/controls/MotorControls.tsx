import { useState } from "react";
import { Square, Gamepad2 } from "lucide-react";
import { useHoldToMove, type MotionAxis } from "../../hooks/useHoldToMove";
import { useGamepad } from "../../hooks/useGamepad";
import { isBlocked, liveSafety, useNow, type ReceivedSafety } from "../sli/sliStatus";

const API_BASE = "http://localhost:8000/api";

// D0 button on the left, D1 on the right. Winch firmware: D0 = wind up, D1 = pay out.
const AXES: { id: MotionAxis; name: string; d0Label: string; d1Label: string; d0Title: string; d1Title: string }[] = [
  { id: 1, name: "Swing",  d0Label: "◄", d1Label: "►", d0Title: "Swing D0", d1Title: "Swing D1" },
  { id: 2, name: "Boom",   d0Label: "▼", d1Label: "▲", d0Title: "Boom down", d1Title: "Boom up" },
  { id: 3, name: "Extend", d0Label: "◄", d1Label: "►", d0Title: "Retract",   d1Title: "Extend" },
  { id: 4, name: "Winch",  d0Label: "▲", d1Label: "▼", d0Title: "Wind up (hook rises)", d1Title: "Pay out (hook lowers)" },
];

interface MotorControlsProps {
  speed: number;
  onSpeedChange: (s: number) => void;
  safety: ReceivedSafety;
}

/**
 * Hold-to-move buttons + Xbox controller. Both go through useHoldToMove, which
 * repeats the command while held; the firmware stops any axis whose command
 * stops arriving for 400 ms (dead-man).
 */
export function MotorControls({ speed, onSpeedChange, safety }: MotorControlsProps) {
  const motion = useHoldToMove();
  const sli = liveSafety(safety, useNow());
  // Shown before pressing; the ESP32 still decides (and the alert bar explains a refusal)
  const cut = (axis: number, dir: 0 | 1) => isBlocked(sli, axis, dir);
  const { connected: gpConnected } = useGamepad(motion, speed);
  const [pressed, setPressed] = useState<string | null>(null);   // "axis-dir" of the held button

  const start = (e: React.PointerEvent<HTMLButtonElement>, axis: MotionAxis, dir: 0 | 1) => {
    // Keep receiving pointerup even if the pointer slides off the button
    e.currentTarget.setPointerCapture(e.pointerId);
    setPressed(`${axis}-${dir}`);
    motion.hold(axis, speed, dir);
  };

  const end = (axis: MotionAxis) => {
    setPressed(null);
    motion.release(axis);
  };

  const stopAll = async () => {
    setPressed(null);
    motion.releaseAll();
    try { await fetch(`${API_BASE}/estop`, { method: "POST" }); } catch { /* offline */ }
  };

  const holdProps = (axis: MotionAxis, dir: 0 | 1) => ({
    onPointerDown: (e: React.PointerEvent<HTMLButtonElement>) => start(e, axis, dir),
    onPointerUp: () => end(axis),
    onPointerCancel: () => end(axis),
    onLostPointerCapture: () => end(axis),
    onContextMenu: (e: React.MouseEvent) => e.preventDefault(),   // long-press on touch
  });

  return (
    <div className="panel" style={{ height: "100%" }}>
      <div className="panel-header">
        <span className="ph-icon">⊕</span>
        Motor Controls
      </div>

      <div className="axes-grid">
        {AXES.map(ax => (
          <div className="axis-card" key={ax.id}>
            <div className="axis-name" title={ax.id === 4 ? "Runs at a fixed 90 % power; the speed slider doesn't apply" : undefined}>
              {ax.name}{ax.id === 4 && <span className="axis-id">90 %</span>}
              <span className="axis-id">M{ax.id}</span>
            </div>
            <div className="axis-btns">
              <button className={`mtr-btn ${pressed === `${ax.id}-0` ? "pressed" : ""} ${cut(ax.id, 0) ? "sli-cut" : ""}`}
                      title={cut(ax.id, 0) ? `${ax.d0Title}: blocked by the SLI` : ax.d0Title} {...holdProps(ax.id, 0)}>
                {ax.d0Label}
              </button>
              <button className="mtr-btn stop" title="Stop" onClick={() => end(ax.id)}>
                <Square size={9} />
              </button>
              <button className={`mtr-btn ${pressed === `${ax.id}-1` ? "pressed" : ""} ${cut(ax.id, 1) ? "sli-cut" : ""}`}
                      title={cut(ax.id, 1) ? `${ax.d1Title}: blocked by the SLI` : ax.d1Title} {...holdProps(ax.id, 1)}>
                {ax.d1Label}
              </button>
            </div>
          </div>
        ))}
      </div>

      <div className="controls-footer" style={{ flexWrap: "wrap", gap: 6 }}>
        <span className="speed-label">
          Speed: <span className="speed-val">{speed}</span>
        </span>
        <input
          type="range" min={250} max={2500} step={10}
          value={speed}
          onChange={e => onSpeedChange(Number(e.target.value))}
          className="speed-slider"
        />
        <button className="stop-all-btn" onClick={stopAll} title="Stop every axis (E-stop)">
          ■ Stop All
        </button>
      </div>

      <div style={{ padding: "0 8px 8px" }}>
        <div className={`gp-badge ${gpConnected ? "live" : ""}`}
             title="Left stick: swing / boom · Right stick: telescope · RT/LT: winch · B: E-stop">
          <Gamepad2 size={12} />
          {gpConnected ? "Controller active" : "No controller"}
        </div>
      </div>
    </div>
  );
}
