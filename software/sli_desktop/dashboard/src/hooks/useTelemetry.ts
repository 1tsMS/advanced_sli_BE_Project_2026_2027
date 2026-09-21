import { useState, useEffect, useRef, useCallback } from "react";
import type { TelemetryFrame, DebugReport, HistoryPoint } from "../types";

const WS_URL = "ws://localhost:8000/ws/telemetry";
const STATUS_URL = "http://localhost:8000/api/status";
const RECONNECT_DELAY = 3000;
/** How much telemetry history the rolling charts keep, in seconds. */
export const HISTORY_SECONDS = 30;

export type WsStatus = "disconnected" | "connecting" | "connected" | "error";

interface UseTelemetryReturn {
  frame: TelemetryFrame | null;
  debugReport: DebugReport | null;
  lastAck: string | null;
  wsStatus: WsStatus;
  espConnected: boolean;
  /** Last HISTORY_SECONDS of samples, oldest first. */
  history: HistoryPoint[];
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

  const wsRef = useRef<WebSocket | null>(null);
  const reconnectTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const lastTelemetryTime = useRef<number>(0);
  const mountedRef = useRef(true);
  const [history, setHistory] = useState<HistoryPoint[]>([]);

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
    };

    ws.onmessage = (event) => {
      if (!mountedRef.current) return;
      try {
        const msg = JSON.parse(event.data as string);
        if (msg.type === "telemetry") {
          const f = msg as TelemetryFrame;
          const point: HistoryPoint = {
            t: Date.now() / 1000,
            boomAngle: f.boomAngle,
            extensionMM: f.extensionMM,
            actualLoad: f.actualLoad,
            safeLoadLimit: f.safeLoadLimit,
            loadPercent: f.loadPercent,
          };
          setHistory(prev => {
            // Drop samples older than the window (they are always at the front)
            const cutoff = point.t - HISTORY_SECONDS;
            let start = 0;
            while (start < prev.length && prev[start].t < cutoff) start++;
            const next = prev.slice(start);
            next.push(point);
            return next;
          });

          setFrame(f);
          lastTelemetryTime.current = Date.now();
          setEspConnected(true);
        } else if (msg.type === "debug") {
          setDebugReport({ entries: msg.entries });
          lastTelemetryTime.current = Date.now();
          setEspConnected(true);
        } else if (msg.type === "ack") {
          setLastAck(msg.data as string);
          lastTelemetryTime.current = Date.now();
          setEspConnected(true);
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

  return { frame, debugReport, lastAck, wsStatus, espConnected, history };
}
