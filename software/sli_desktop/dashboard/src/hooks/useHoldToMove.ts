import { useCallback, useEffect, useRef } from "react";
import { API_BASE } from "../api";


/** Resend interval while an axis is held. Firmware stops an axis after 400 ms of silence. */
const REPEAT_MS = 100;

export type MotionAxis = 1 | 2 | 3 | 4;   // 1 swing, 2 boom lift, 3 telescope, 4 winch

async function send(command: string) {
  try {
    await fetch(`${API_BASE}/command`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ command }),
    });
  } catch {
    /* backend offline: the firmware dead-man stops the motor anyway */
  }
}

/**
 * Dead-man motion: while an axis is held, its M-command is resent every 100 ms.
 * If the browser freezes, the tab is hidden, the window loses focus, a release
 * event is missed or the link drops, the commands stop and the firmware halts
 * the axis within 400 ms. Release also sends an explicit M0 right away.
 *
 * Used by the on-screen buttons, the Xbox controller and calibration jogs, so
 * every motion source behaves the same.
 */
export function useHoldToMove() {
  const held = useRef(new Map<MotionAxis, string>());   // axis -> current M command

  const hold = useCallback((axis: MotionAxis, speed: number, dir: 0 | 1) => {
    const cmd = `M${axis} S${Math.max(1, Math.min(2500, Math.round(speed)))} D${dir}`;
    if (held.current.get(axis) === cmd) return;          // unchanged: the timer resends it
    held.current.set(axis, cmd);
    send(cmd);
  }, []);

  // A repeat that was already in flight can reach the ESP32 after the M0 and
  // restart the axis until the dead-man expires; a second M0 shortly after
  // closes that window.
  const stop = useCallback((axis: MotionAxis) => {
    send(`M0 A${axis}`);
    setTimeout(() => { if (!held.current.has(axis)) send(`M0 A${axis}`); }, 150);
  }, []);

  const release = useCallback((axis: MotionAxis) => {
    if (!held.current.has(axis)) return;
    held.current.delete(axis);
    stop(axis);
  }, [stop]);

  const releaseAll = useCallback(() => {
    for (const axis of Array.from(held.current.keys())) {
      held.current.delete(axis);
      stop(axis);
    }
  }, [stop]);

  useEffect(() => {
    const timer = setInterval(() => {
      held.current.forEach(cmd => send(cmd));
    }, REPEAT_MS);
    const onHidden = () => { if (document.visibilityState !== "visible") releaseAll(); };
    window.addEventListener("blur", releaseAll);
    document.addEventListener("visibilitychange", onHidden);
    return () => {
      clearInterval(timer);
      window.removeEventListener("blur", releaseAll);
      document.removeEventListener("visibilitychange", onHidden);
      releaseAll();
    };
  }, [releaseAll]);

  return { hold, release, releaseAll };
}
