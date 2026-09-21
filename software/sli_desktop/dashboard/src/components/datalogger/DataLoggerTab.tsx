import { useCallback, useEffect, useState } from "react";
import { Download, RefreshCw } from "lucide-react";
import { API_BASE, apiJson } from "../../api";
import { HISTORY_SECONDS } from "../../hooks/useTelemetry";
import type { HistoryPoint } from "../../types";
import { LineChart } from "../shared/LineChart";

interface SessionFile {
  filename: string;
  size: number;
  modified: string;
}

interface DataLoggerTabProps {
  history: HistoryPoint[];
  /** True while telemetry is arriving from the ESP32 */
  connected: boolean;
}

const fetchSessions = () => apiJson<{ sessions: SessionFile[] }>("/logs");

function formatSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
}

export function DataLoggerTab({ history, connected }: DataLoggerTabProps) {
  const [sessions, setSessions] = useState<SessionFile[]>([]);
  const [error, setError] = useState("");
  const [loading, setLoading] = useState(false);

  const applyResult = useCallback((res: Awaited<ReturnType<typeof fetchSessions>>) => {
    if (res.ok && res.data) {
      setSessions(res.data.sessions);
      setError("");
    } else {
      setError(res.error ?? "Could not load sessions");
    }
  }, []);

  const refresh = useCallback(async () => {
    setLoading(true);
    const res = await fetchSessions();
    setLoading(false);
    applyResult(res);
  }, [applyResult]);

  // Initial load (state is only set after the request resolves)
  useEffect(() => {
    let cancelled = false;
    fetchSessions().then(res => { if (!cancelled) applyResult(res); });
    return () => { cancelled = true; };
  }, [applyResult]);

  return (
    <div style={{ flex: 1, overflowY: "auto", padding: 10, display: "flex", flexDirection: "column", gap: 10 }}>

      {/* Live rolling charts */}
      <div className="debug-card" style={{ flexShrink: 0 }}>
        <div className="debug-card-header">
          <span className="debug-card-title">Live — last {HISTORY_SECONDS} s</span>
          <span className="dim-text" style={{ fontSize: 10.5 }}>
            {connected ? `${history.length} samples` : "No telemetry (connect the ESP32 in Settings)"}
          </span>
        </div>
        <div
          className="debug-card-body"
          style={{ overflow: "visible", display: "grid", gap: 12,
                   gridTemplateColumns: "repeat(auto-fit, minmax(420px, 1fr))" }}
        >
          <LineChart
            title="Load" unit="kg" points={history} windowSeconds={HISTORY_SECONDS}
            series={[
              { label: "Load",  color: "var(--cyan)",  pick: p => p.actualLoad },
              { label: "Limit", color: "var(--amber)", pick: p => p.safeLoadLimit, dashed: true },
            ]}
          />
          <LineChart
            title="Boom angle" unit="deg" points={history} windowSeconds={HISTORY_SECONDS}
            series={[{ label: "Angle", color: "var(--green)", pick: p => p.boomAngle }]}
          />
          <LineChart
            title="Telescope extension" unit="mm" points={history} windowSeconds={HISTORY_SECONDS}
            series={[{ label: "Extension", color: "var(--cyan)", pick: p => p.extensionMM }]}
          />
        </div>
      </div>

      {/* Saved session files */}
      <div className="debug-card" style={{ flexShrink: 0 }}>
        <div className="debug-card-header">
          <span className="debug-card-title">Saved sessions (CSV)</span>
          <button className="btn-ghost" onClick={refresh} disabled={loading}
                  style={{ display: "flex", alignItems: "center", gap: 5 }}>
            <RefreshCw size={10} /> Refresh
          </button>
        </div>
        <div className="debug-card-body" style={{ overflow: "visible" }}>
          {error && <div className="fail-text" style={{ fontSize: 11 }}>{error}</div>}
          {!error && sessions.length === 0 && (
            <div className="dim-text" style={{ fontSize: 11 }}>
              No sessions yet. A new CSV is started every time you connect to the ESP32.
            </div>
          )}
          {sessions.length > 0 && (
            <table className="scan-table">
              <thead>
                <tr><th>File</th><th>Modified</th><th>Size</th><th /></tr>
              </thead>
              <tbody>
                {sessions.map(s => (
                  <tr key={s.filename}>
                    <td>{s.filename}</td>
                    <td className="dim-text">{s.modified.replace("T", " ").slice(0, 19)}</td>
                    <td className="dim-text">{formatSize(s.size)}</td>
                    <td style={{ textAlign: "right" }}>
                      <a className="btn-ghost" href={`${API_BASE}/logs/${encodeURIComponent(s.filename)}`}
                         download={s.filename} style={{ textDecoration: "none" }}>
                        <Download size={10} /> Download
                      </a>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </div>
      </div>
    </div>
  );
}
