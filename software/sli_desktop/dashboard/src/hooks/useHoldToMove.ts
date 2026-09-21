import { useCallback, useEffect, useMemo, useRef } from "react";

const API_BASE = "http://localhost:8000/api";

// The firmware stops an axis if its motion command isn't repeated within
// MOTION_TIMEOUT_MS (400 ms). Resend well inside that window.
const HEARTBEAT_MS = 100;

async function sendCommand(command: string): Promise<void> {
  try {
    await fetch(`${API_BASE}/command`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ command }),
    });
  } catch {
    // Backend offline — the firmware watchdog will stop the axis anyway
  }
}

/**
 * Dead-man hold-to-move.
 *
 * start() sends the motion command immediately and then repeats it every
 * HEARTBEAT_MS. If the page stops sending (button released, window loses
 * focus, tab hidden, component unmounted, browser frozen, backend down) the
 * heartbeat stops and the firmware watchdog halts the axis.
 */
export function useHoldToMove() {
  const timers = useRef(new Map<number, ReturnType<typeof setInterval>>());

  const cancel = useCallback((axis: number) => {
    const t = timers.current.get(axis);
    if (t !== undefined) {
      clearInterval(t);
      timers.current.delete(axis);
    }
  }, []);

  const start = useCallback((axis: number, speed: number, dir: 0 | 1) => {
    cancel(axis);
    const command = `M${axis} S${speed} D${dir}`;
    void sendCommand(command);
    timers.current.set(axis, setInterval(() => void sendCommand(command), HEARTBEAT_MS));
  }, [cancel]);

  /** Stop heartbeating and tell the firmware to stop this axis now. */
  const stop = useCallback((axis: number) => {
    cancel(axis);
    return sendCommand(`M0 A${axis}`);
  }, [cancel]);

  /** Stop heartbeating on every axis without sending anything (used with E-stop). */
  const cancelAll = useCallback(() => {
    for (const axis of Array.from(timers.current.keys())) cancel(axis);
  }, [cancel]);

  /** Stop every axis that is currently being held. */
  const stopAll = useCallback(() => {
    for (const axis of Array.from(timers.current.keys())) void stop(axis);
  }, [stop]);

  // Releasing the mouse outside the window, alt-tabbing or hiding the tab
  // never delivers a mouseup — halt everything held when focus is lost.
  useEffect(() => {
    const onHidden = () => { if (document.hidden) stopAll(); };
    window.addEventListener("blur", stopAll);
    document.addEventListener("visibilitychange", onHidden);
    return () => {
      window.removeEventListener("blur", stopAll);
      document.removeEventListener("visibilitychange", onHidden);
      stopAll();
    };
  }, [stopAll]);

  return useMemo(
    () => ({ start, stop, cancelAll, stopAll }),
    [start, stop, cancelAll, stopAll],
  );
}
