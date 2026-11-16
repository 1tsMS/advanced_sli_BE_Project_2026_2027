import { useState, useEffect, useRef, useCallback, type RefObject } from "react";
import type { TelemetryFrame, DebugReport, SafetyStatus, LoadChartProfile, LivePoint, ShapingState, Capture } from "../types";
import { ShockFilter, PeakHold } from "../components/telemetry/shock";
import { API_BASE, WS_URL } from "../api";

const STATUS_URL = `${API_BASE}/status`;
const RECONNECT_DELAY = 3000;
const HISTORY_S = 60;          // rolling window kept for the live charts
export const CAPTURE_S = 300;  // "5-min capture" length
const FRAME_UI_MS = 66;        // readouts redraw at ~15 Hz; the charts keep every 50 Hz frame (history ref)
const FAULT_HOLD_S = 2;        // SLI FAULT: keep showing the last capacity / load % this long

export type WsStatus = "disconnected" | "connecting" | "connected" | "error";

interface UseTelemetryReturn {
  frame: TelemetryFrame | null;
  debugReport: DebugReport | null;
  lastAck: string | null;
  wsStatus: WsStatus;
  espConnected: boolean;
  /** Latest $S from the ESP32 (null until one arrives). receivedAt drives the stale check. */
  safety: (SafetyStatus & { receivedAt: number }) | null;
  /** Chart read back from the ESP32 (LC GET): undefined = not asked, null = the ESP32 has none. */
  craneChart: { chart: LoadChartProfile | null; receivedAt: number } | undefined;
  /** Last load chart COMMIT result ($ACK,LC,COMMITTED / ERR). seq counts up, so a repeat of the
   *  same text (same chart uploaded twice) is still a new result. */
  lcResult: { text: string; seq: number };
  /** Last motion the SLI refused or cut ($ACK,SLI,REFUSED / CUT,...); seq counts up per event. */
  sliBlock: { text: string; seq: number };
  /** Last HISTORY_S seconds of telemetry, oldest first. A ref (not state): the live
   *  charts read it on their own timer, so 50 Hz frames don't re-render them. */
  history: RefObject<LivePoint[]>;
  /** 5-min capture (Data > Live): filled from start() until CAPTURE_S, then stops by itself. */
  capture: RefObject<Capture>;
  startCapture: () => void;
  stopCapture: () => void;
  /** Swing input shaping as last reported by the ESP32 (null = not reported since connect). */
  shaping: ShapingState | null;
}

/**
 * useTelemetry — connects to the Python backend WebSocket and
 * delivers parsed telemetry frames, debug reports, and ACKs.
 * Tracks both backend WebSocket connection AND ESP32 serial connection.
 */
export function useTelemetry(): UseTelemetryReturn {
  const [frame, setFrame] = useState<TelemetryFrame | null>(null);
  const [debugReport, setDebugReport] = useState<DebugReport | null>(null);
  const [lastAck, setLastAck] = useState<string | null>(null);
  const [wsStatus, setWsStatus] = useState<WsStatus>("disconnected");
  const [espConnected, setEspConnected] = useState<boolean>(false);
  const [safety, setSafety] = useState<UseTelemetryReturn["safety"]>(null);
  const [craneChart, setCraneChart] = useState<UseTelemetryReturn["craneChart"]>(undefined);
  const [lcResult, setLcResult] = useState<UseTelemetryReturn["lcResult"]>({ text: "", seq: 0 });
  const [sliBlock, setSliBlock] = useState<UseTelemetryReturn["sliBlock"]>({ text: "", seq: 0 });
  const [shaping, setShaping] = useState<ShapingState | null>(null);
  const history = useRef<LivePoint[]>([]);
  const capture = useRef<Capture>({ active: false, t0: 0, points: [] });
  const shock = useRef({ accel: new ShockFilter(250, 0.05), jerk: new ShockFilter(250, 2), peak: new PeakHold(2000) });
  const startCapture = useCallback(() => { capture.current = { active: true, t0: 0, points: [] }; }, []);
  const stopCapture = useCallback(() => { capture.current.active = false; }, []);

  const wsRef = useRef<WebSocket | null>(null);
  const reconnectTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const lastTelemetryTime = useRef<number>(0);
  const mountedRef = useRef(true);
  const lastFrameSet = useRef(0);
  const sliState = useRef<string>("");
  const lastRated = useRef<{ cap: number; pct: number; ts: number } | null>(null);

  // Poll backend status for ESP32 serial link
  const checkStatus = useCallback(async () => {
    if (!mountedRef.current) return;
    try {
      const res = await fetch(STATUS_URL);
      const data = await res.json();
      const hasRecentData = Date.now() - lastTelemetryTime.current < 2500;
      setEspConnected(!!data.connected || hasRecentData);
    } catch {
      const hasRecentData = Date.now() - lastTelemetryTime.current < 2500;
      setEspConnected(hasRecentData);
    }
  }, []);

  const connect = useCallback(() => {
    if (!mountedRef.current) return;

    setWsStatus("connecting");
    const ws = new WebSocket(WS_URL);
    wsRef.current = ws;

    ws.onopen = () => {
      if (!mountedRef.current) return;
      setWsStatus("connected");
      checkStatus();
      // Ask the ESP32 which chart it enforces, so the dashboard can say WHY a pose is outside it
      fetch(`${API_BASE}/loadchart/readback`, { method: "POST" }).catch(() => null);
    };

    ws.onmessage = (event) => {
      if (!mountedRef.current) return;
      try {
        const msg = JSON.parse(event.data as string);
        if (msg.type === "telemetry") {
          const f = msg as TelemetryFrame;
          // Shocks only: the idle wobble is taken out before anything is drawn
          const s = shock.current;
          f.accelShock = s.accel.push(f.accelDyn ?? 0);
          f.jerkShock = s.jerk.push(f.jerk ?? 0);
          f.accelPeak = s.peak.push(f.accelShock, f.timestamp * 1000);
          // In FAULT the ESP32 reports capacity 0 / 0 %. A short sensor dropout would
          // flash the readouts and cut the charts, so the last real values are shown
          // for up to FAULT_HOLD_S (the SLI badge still says FAULT; longer = shown as is).
          if (f.safeLoadLimit > 0) {
            lastRated.current = { cap: f.safeLoadLimit, pct: f.loadPercent, ts: f.timestamp };
          } else if (sliState.current === "FAULT" && lastRated.current &&
                     f.timestamp - lastRated.current.ts <= FAULT_HOLD_S) {
            f.safeLoadLimit = lastRated.current.cap;
            f.loadPercent = lastRated.current.pct;
          }
          const nowMs = performance.now();
          if (nowMs - lastFrameSet.current >= FRAME_UI_MS) {
            lastFrameSet.current = nowMs;
            setFrame(f);
          }
          const p: LivePoint = {
            t: 0, ts: f.timestamp, load: f.actualLoad, cap: f.safeLoadLimit,
            pct: f.safeLoadLimit > 0 ? f.loadPercent : null,
            boom: f.boomAngle, swing: f.swingAngle, ext: f.extensionMM, hookDrop: f.hookDrop,
            tip: f.tipValid ? (f.tipMargin ?? null) : null,
            accel: f.accelShock, jerk: f.jerkShock,
          };
          const h = history.current;
          h.push(p);
          while (h.length && f.timestamp - h[0].ts > HISTORY_S) h.shift();
          const c = capture.current;
          if (c.active) {
            if (!c.points.length) c.t0 = f.timestamp;
            if (f.timestamp - c.t0 <= CAPTURE_S) c.points.push(p);
            else c.active = false;
          }
          lastTelemetryTime.current = Date.now();
          setEspConnected(true);
        } else if (msg.type === "debug") {
          setDebugReport({ entries: msg.entries });
          lastTelemetryTime.current = Date.now();
          setEspConnected(true);
        } else if (msg.type === "ack") {
          const ack = msg.data as string;
          setLastAck(ack);
          if (ack.startsWith("$ACK,LC,COMMITTED") || ack.startsWith("$ACK,LC,ERR")) {
            setLcResult(r => ({ text: ack, seq: r.seq + 1 }));
          } else if (ack.startsWith("$ACK,SLI,REFUSED") || ack.startsWith("$ACK,SLI,CUT")) {
            setSliBlock(r => ({ text: ack, seq: r.seq + 1 }));
          }
          lastTelemetryTime.current = Date.now();
          setEspConnected(true);
        } else if (msg.type === "safety") {
          sliState.current = (msg as SafetyStatus).state;
          setSafety({ ...(msg as SafetyStatus), receivedAt: Date.now() });
        } else if (msg.type === "shaping") {
          setShaping({ on: !!msg.on, lengthMm: msg.lengthMm, halfPeriodMs: msg.halfPeriodMs });
        } else if (msg.type === "chart") {
          const c = msg.chart as Omit<LoadChartProfile, "description"> | null;
          setCraneChart({ chart: c ? { description: "", ...c } : null, receivedAt: Date.now() });
        }
      } catch {
        // Ignore malformed messages
      }
    };

    ws.onerror = () => {
      if (!mountedRef.current) return;
      setWsStatus("error");
    };

    ws.onclose = () => {
      if (!mountedRef.current) return;
      setWsStatus("disconnected");
      setEspConnected(false);
      setShaping(null);
      reconnectTimer.current = setTimeout(connect, RECONNECT_DELAY);
    };
  }, [checkStatus]);

  useEffect(() => {
    mountedRef.current = true;
    connect();

    const statusInterval = setInterval(checkStatus, 2000);

    return () => {
      mountedRef.current = false;
      clearInterval(statusInterval);
      if (reconnectTimer.current) clearTimeout(reconnectTimer.current);
      wsRef.current?.close();
    };
  }, [connect, checkStatus]);

  return { frame, debugReport, lastAck, wsStatus, espConnected,
           safety, craneChart, lcResult, sliBlock, history, capture, startCapture, stopCapture, shaping };
}
