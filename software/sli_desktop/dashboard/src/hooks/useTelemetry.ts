import { useState, useEffect, useRef, useCallback, type RefObject } from "react";
import type { TelemetryFrame, DebugReport, RecordingState, SafetyStatus, LoadChartProfile, LivePoint, ShapingState } from "../types";

const WS_URL = "ws://localhost:8000/ws/telemetry";
const STATUS_URL = "http://localhost:8000/api/status";
const RECONNECT_DELAY = 3000;
const HISTORY_S = 60;          // rolling window kept for the live charts

export type WsStatus = "disconnected" | "connecting" | "connected" | "error";

interface UseTelemetryReturn {
  frame: TelemetryFrame | null;
  debugReport: DebugReport | null;
  lastAck: string | null;
  wsStatus: WsStatus;
  espConnected: boolean;
  /** receivedAt: Date.now() when this state arrived (lets the REC timer tick between polls) */
  recording: RecordingState & { receivedAt: number };
  /** Re-read /api/status now (e.g. right after toggling recording). */
  refreshStatus: () => void;
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
  /** Swing input shaping as last reported by the ESP32 (null = not reported since connect). */
  shaping: ShapingState | null;
}

const NOT_RECORDING: RecordingState = { recording: false, file: null, rows: 0, elapsedSec: 0 };

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
  const [recording, setRecording] = useState<RecordingState & { receivedAt: number }>(
    () => ({ ...NOT_RECORDING, receivedAt: 0 })
  );
  const [safety, setSafety] = useState<UseTelemetryReturn["safety"]>(null);
  const [craneChart, setCraneChart] = useState<UseTelemetryReturn["craneChart"]>(undefined);
  const [lcResult, setLcResult] = useState<UseTelemetryReturn["lcResult"]>({ text: "", seq: 0 });
  const [sliBlock, setSliBlock] = useState<UseTelemetryReturn["sliBlock"]>({ text: "", seq: 0 });
  const [shaping, setShaping] = useState<ShapingState | null>(null);
  const history = useRef<LivePoint[]>([]);

  const wsRef = useRef<WebSocket | null>(null);
  const reconnectTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const lastTelemetryTime = useRef<number>(0);
  const mountedRef = useRef(true);

  // Poll backend status for ESP32 serial link
  const checkStatus = useCallback(async () => {
    if (!mountedRef.current) return;
    try {
      const res = await fetch(STATUS_URL);
      const data = await res.json();
      const hasRecentData = Date.now() - lastTelemetryTime.current < 2500;
      setEspConnected(!!data.connected || hasRecentData);
      setRecording({ ...(data.recording ?? NOT_RECORDING), receivedAt: Date.now() });
    } catch {
      const hasRecentData = Date.now() - lastTelemetryTime.current < 2500;
      setEspConnected(hasRecentData);
      setRecording({ ...NOT_RECORDING, receivedAt: Date.now() });
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
    };

    ws.onmessage = (event) => {
      if (!mountedRef.current) return;
      try {
        const msg = JSON.parse(event.data as string);
        if (msg.type === "telemetry") {
          const f = msg as TelemetryFrame;
          setFrame(f);
          const h = history.current;
          h.push({ t: 0, ts: f.timestamp, load: f.actualLoad, cap: f.safeLoadLimit, pct: f.loadPercent,
                   boom: f.boomAngle, swing: f.swingAngle, ext: f.extensionMM, hookDrop: f.hookDrop,
                   tip: f.tipValid ? (f.tipMargin ?? null) : null });
          while (h.length && f.timestamp - h[0].ts > HISTORY_S) h.shift();
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

  return { frame, debugReport, lastAck, wsStatus, espConnected, recording, refreshStatus: checkStatus,
           safety, craneChart, lcResult, sliBlock, history, shaping };
}
