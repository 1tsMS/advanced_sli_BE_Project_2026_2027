import { useCallback, useState, useEffect } from "react";
import { Square, Gamepad2 } from "lucide-react";
import { useHoldToMove } from "../../hooks/useHoldToMove";
import { useGamepad } from "../../hooks/useGamepad";
import { AXES, type AxisId } from "../../config/axes";

const API_BASE = "http://localhost:8000/api";

async function apiPost(path: string, body: object) {
  try {
    const res = await fetch(`${API_BASE}${path}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
    return res.ok;
  } catch { return false; }
}

interface MotorControlsProps {
  speed: number;
  onSpeedChange: (s: number) => void;
}

export function MotorControls({ speed, onSpeedChange }: MotorControlsProps) {
  const [pressed, setPressed] = useState<Record<string, boolean>>({});
  const [gpConnected, setGpConnected] = useState(false);
  const hold = useHoldToMove();

  useEffect(() => {
    const on  = () => setGpConnected(true);
    const off = () => setGpConnected(false);
    window.addEventListener("gamepadconnected", on);
    window.addEventListener("gamepaddisconnected", off);
    return () => {
      window.removeEventListener("gamepadconnected", on);
      window.removeEventListener("gamepaddisconnected", off);
    };
  }, []);

  // Hold-to-move is a dead-man: the command is repeated while held, and the
  // firmware stops the axis if the repeats stop (see useHoldToMove).
  const startMotor = useCallback((axis: AxisId, dir: 0 | 1) => {
    const key = `${axis}-${dir}`;
    if (pressed[key]) return;
    setPressed(p => ({ ...p, [key]: true }));
    hold.start(axis, speed, dir);
  }, [speed, pressed, hold]);

  const stopAxis = useCallback(async (axis: AxisId) => {
    setPressed(p => {
      const n = { ...p };
      delete n[`${axis}-0`]; delete n[`${axis}-1`];
      return n;
    });
    await hold.stop(axis);
  }, [hold]);

  const stopAll = useCallback(async () => {
    setPressed({});
    hold.cancelAll();          // no more heartbeats, then latch the E-stop
    await apiPost("/estop", {});
  }, [hold]);

  // Xbox / standard gamepad: same dead-man path as the buttons; B = E-stop
  useGamepad({ enabled: gpConnected, maxSpeed: speed, hold, onEstop: stopAll });

  return (
    <div className="panel" style={{ height: "100%" }}>
      <div className="panel-header">
        <span className="ph-icon">⊕</span>
        Motor Controls
      </div>

      {/* 2×2 axis grid */}
      <div className="axes-grid">
        {AXES.map(ax => (
          <div className="axis-card" key={ax.id}>
            <div className="axis-name">
              {ax.name}
              <span className="axis-id">M{ax.id}</span>
            </div>
            <div className="axis-btns">
              <button
                className={`mtr-btn ${pressed[`${ax.id}-${ax.negDir}`] ? "pressed" : ""}`}
                onMouseDown={() => startMotor(ax.id, ax.negDir)}
                onMouseUp={() => stopAxis(ax.id)}
                onMouseLeave={() => { if (pressed[`${ax.id}-${ax.negDir}`]) stopAxis(ax.id); }}
                onTouchStart={e => { e.preventDefault(); startMotor(ax.id, ax.negDir); }}
                onTouchEnd={() => stopAxis(ax.id)}
              >
                {ax.negLabel}
              </button>
              <button
                className="mtr-btn stop"
                title="Stop"
                onClick={() => stopAxis(ax.id)}
              >
                <Square size={9} />
              </button>
              <button
                className={`mtr-btn ${pressed[`${ax.id}-${ax.posDir}`] ? "pressed" : ""}`}
                onMouseDown={() => startMotor(ax.id, ax.posDir)}
                onMouseUp={() => stopAxis(ax.id)}
                onMouseLeave={() => { if (pressed[`${ax.id}-${ax.posDir}`]) stopAxis(ax.id); }}
                onTouchStart={e => { e.preventDefault(); startMotor(ax.id, ax.posDir); }}
                onTouchEnd={() => stopAxis(ax.id)}
              >
                {ax.posLabel}
              </button>
            </div>
          </div>
        ))}
      </div>

      {/* Footer: speed + stop all + gamepad */}
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
        <button className="stop-all-btn" onClick={stopAll}>
          ■ Stop All
        </button>
      </div>

      {/* Gamepad indicator */}
      <div style={{ padding: "0 8px 8px" }}>
        <div className={`gp-badge ${gpConnected ? "live" : ""}`}>
          <Gamepad2 size={12} />
          {gpConnected ? "Controller active" : "No controller"}
        </div>
      </div>
    </div>
  );
}
