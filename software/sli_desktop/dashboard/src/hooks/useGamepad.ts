import { useEffect, useRef, useState } from "react";
import type { MotionAxis } from "./useHoldToMove";
import { API_BASE } from "../api";

const DEADZONE = 0.15;

interface MotionControl {
  hold: (axis: MotionAxis, speed: number, dir: 0 | 1) => void;
  release: (axis: MotionAxis) => void;
}

/**
 * Xbox controller (W3C Gamepad API), driven through the same dead-man
 * hold/release path as the on-screen buttons.
 *
 *   Left stick X   -> Swing (M1)        Left stick Y  -> Boom lift (M2, up = D1)
 *   Right stick Y  -> Telescope (M3)    RT / LT       -> Winch pay out (D1) / wind up (D0)
 *   B              -> E-stop (once per press)
 *
 * Speed = stick deflection x the speed slider. Directions use the same D values
 * as the on-screen buttons; flip a sign below if an axis moves the wrong way.
 */
export function useGamepad(motion: MotionControl, maxSpeed: number) {
  const [connected, setConnected] = useState(false);
  const maxSpeedRef = useRef(maxSpeed);
  const motionRef = useRef(motion);
  useEffect(() => { maxSpeedRef.current = maxSpeed; }, [maxSpeed]);
  useEffect(() => { motionRef.current = motion; }, [motion]);

  useEffect(() => {
    let raf = 0;
    let bWasDown = false;
    const active = new Set<MotionAxis>();

    const drive = (axis: MotionAxis, value: number) => {
      const m = motionRef.current;
      if (Math.abs(value) < DEADZONE) {
        if (active.has(axis)) { active.delete(axis); m.release(axis); }
        return;
      }
      active.add(axis);
      // Rescale so the speed ramps from 0 at the deadzone edge
      const mag = (Math.abs(value) - DEADZONE) / (1 - DEADZONE);
      // Quantise to 50-step increments so small stick wobble doesn't send a new command every frame
      const speed = Math.max(50, Math.round((mag * maxSpeedRef.current) / 50) * 50);
      m.hold(axis, speed, value > 0 ? 1 : 0);
    };

    const tick = () => {
      const gp = navigator.getGamepads?.()[0];
      setConnected(prev => (prev !== !!gp ? !!gp : prev));
      if (gp) {
        const bDown = gp.buttons[1]?.pressed ?? false;
        if (bDown && !bWasDown) {
          fetch(`${API_BASE}/estop`, { method: "POST" }).catch(() => {});
        }
        bWasDown = bDown;

        drive(1, gp.axes[0] ?? 0);
        drive(2, -(gp.axes[1] ?? 0));          // stick up is negative: up = boom up (D1)
        drive(3, -(gp.axes[3] ?? 0));          // right stick up = extend (D1)
        const rt = gp.buttons[7]?.value ?? 0;
        const lt = gp.buttons[6]?.value ?? 0;
        drive(4, rt > lt ? rt : -lt);          // RT pays out (D1), LT winds up (D0)
      } else if (active.size) {
        active.forEach(a => motionRef.current.release(a));
        active.clear();
      }
      raf = requestAnimationFrame(tick);
    };

    raf = requestAnimationFrame(tick);
    return () => {
      cancelAnimationFrame(raf);
      active.forEach(a => motionRef.current.release(a));
    };
  }, []);

  return { connected };
}
