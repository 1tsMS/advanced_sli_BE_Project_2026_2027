import { useEffect, useRef } from "react";
import { AXES, type AxisId } from "../config/axes";
import type { useHoldToMove } from "./useHoldToMove";

type Hold = ReturnType<typeof useHoldToMove>;

const DEADZONE = 0.15;
/** Slowest speed sent once a stick leaves the deadzone (steps/s) */
const MIN_SPEED = 50;
/** Resend with a new speed only when it moved by this fraction of max speed */
const SPEED_STEP = 0.1;

interface UseGamepadOptions {
  /** Poll the controller only while true (i.e. one is connected) */
  enabled: boolean;
  /** Speed at full stick deflection — the dashboard speed slider */
  maxSpeed: number;
  /** Shared dead-man hold-to-move controller (repeats commands while held) */
  hold: Hold;
  /** Called once each time B is pressed */
  onEstop: () => void;
}

/**
 * Stick/trigger positions in -1..1 per axis, where positive means the axis's
 * "pos" button on screen (see config/axes.ts). Standard (Xbox) mapping:
 *   Left stick X  → Swing   (right = ►)
 *   Left stick Y  → Boom    (up = ▲)
 *   Right stick Y → Extend  (up = ►)
 *   LT / RT       → Winch   (LT = ▲ up, RT = ▼ down)
 */
function readAxes(gp: Gamepad): Record<AxisId, number> {
  const trigger = (i: number) => gp.buttons[i]?.value ?? 0;
  return {
    1: gp.axes[0] ?? 0,
    2: -(gp.axes[1] ?? 0),
    3: -(gp.axes[3] ?? 0),
    4: trigger(6) - trigger(7),
  };
}

/**
 * Drives the crane from a gamepad. Motion goes through useHoldToMove, so the
 * firmware dead-man applies: if this page stops sending (unplugged controller,
 * lost focus, frozen tab) the axes stop on their own.
 *
 * B is an E-stop and is edge-triggered (one E-stop per press, not one per frame).
 */
export function useGamepad({ enabled, maxSpeed, hold, onEstop }: UseGamepadOptions): void {
  // Latest values for the polling loop without restarting it on every change
  const maxSpeedRef = useRef(maxSpeed);
  const onEstopRef = useRef(onEstop);
  useEffect(() => {
    maxSpeedRef.current = maxSpeed;
    onEstopRef.current = onEstop;
  });

  useEffect(() => {
    if (!enabled) return;

    const active = new Map<AxisId, { dir: 0 | 1; speed: number }>();
    let estopHeld = false;
    let raf = 0;

    const stopAxis = (id: AxisId) => {
      active.delete(id);
      void hold.stop(id);
    };

    const tick = () => {
      const gp = Array.from(navigator.getGamepads()).find((g): g is Gamepad => !!g && g.connected);

      if (!gp) {
        // Controller vanished mid-move: stop everything it was driving
        for (const id of Array.from(active.keys())) stopAxis(id);
        estopHeld = false;
      } else {
        const bPressed = gp.buttons[1]?.pressed ?? false;
        if (bPressed && !estopHeld) onEstopRef.current();
        estopHeld = bPressed;

        const values = readAxes(gp);
        for (const ax of AXES) {
          const raw = values[ax.id];
          const mag = Math.abs(raw);
          const cur = active.get(ax.id);

          if (mag < DEADZONE) {
            if (cur) stopAxis(ax.id);
            continue;
          }

          const dir = raw > 0 ? ax.posDir : ax.negDir;
          if (cur && cur.dir !== dir) {
            // Reversing: stop first and restart on the next frame instead of
            // flipping the direction of a moving motor
            stopAxis(ax.id);
            continue;
          }

          const scaled = (mag - DEADZONE) / (1 - DEADZONE);
          const speed = Math.max(MIN_SPEED, Math.round(scaled * maxSpeedRef.current));
          if (!cur || Math.abs(speed - cur.speed) > maxSpeedRef.current * SPEED_STEP) {
            active.set(ax.id, { dir, speed });
            hold.start(ax.id, speed, dir);
          }
        }
      }

      raf = requestAnimationFrame(tick);
    };

    raf = requestAnimationFrame(tick);
    return () => {
      cancelAnimationFrame(raf);
      for (const id of Array.from(active.keys())) stopAxis(id);
    };
  }, [enabled, hold]);
}
