import { useState, useEffect } from "react";
import { PlugZap, Unplug, Wifi, WifiOff, RefreshCcw } from "lucide-react";
import { TippingCard } from "./TippingCard";

const API_BASE = "http://localhost:8000/api";

interface PortEntry { port: string; description: string; }
interface BackendStatus {
  connected: boolean;
  port?: string;
  baudRate?: number;
  rxRate?: number;
  backendOK: boolean;
}

interface SettingsTabProps {
  wsConnected: boolean;
}

export function SettingsTab({ wsConnected }: SettingsTabProps) {
  const [ports, setPorts]       = useState<PortEntry[]>([]);
  const [selectedPort, setPort] = useState("");
  const [baud, setBaud]         = useState(115200);
  const [status, setStatus]     = useState<BackendStatus | null>(null);
  const [msg, setMsg]           = useState("");
  const [loading, setLoading]   = useState(false);

  const fetchPorts = async () => {
    try {
      const res = await fetch(`${API_BASE}/ports`);
      const d = await res.json();
      setPorts(d.ports ?? []);
    } catch { setPorts([]); }
  };

  const fetchStatus = async () => {
    try {
      const res = await fetch(`${API_BASE}/status`);
      setStatus(await res.json());
    } catch { setStatus(null); }
  };

  useEffect(() => {
    fetchPorts();
    fetchStatus();
    const t = setInterval(fetchStatus, 2000);
    return () => clearInterval(t);
  }, []);

  const connect = async () => {
    if (!selectedPort) { setMsg("Select a port first."); return; }
    setLoading(true); setMsg("");
    try {
      const res = await fetch(`${API_BASE}/connect`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ port: selectedPort, baudRate: baud }),
      });
      const d = await res.json();
      setMsg(d.message ?? (res.ok ? "Connected" : "Failed"));
      await fetchStatus();
    } catch { setMsg("Backend unreachable"); }
    finally { setLoading(false); }
  };

  const disconnect = async () => {
    setLoading(true);
    try {
      await fetch(`${API_BASE}/disconnect`, { method: "POST" });
      setMsg("Disconnected");
      await fetchStatus();
    } catch { setMsg("Error"); }
    finally { setLoading(false); }
  };

  const isConn = !!status?.connected;

  return (
    <div className="settings-page">
      <div className="settings-grid">

        {/* Connection card */}
        <div className="settings-card">
          <div className="section-label">Serial Connection</div>

          <div className="form-row">
            <span className="form-label">Port</span>
            <select
              className="form-select"
              value={selectedPort}
              onChange={e => setPort(e.target.value)}
            >
              <option value="">— select —</option>
              {ports.map(p => (
                <option key={p.port} value={p.port}>
                  {p.port}
                </option>
              ))}
            </select>
            <button
              className="btn-ghost"
              onClick={fetchPorts}
              style={{ padding: "8px 12px", flexShrink: 0 }}
              title="Refresh ports"
            >
              <RefreshCcw size={14} />
            </button>
          </div>

          {/* Port description */}
          {selectedPort && (
            <div style={{
              fontSize: 11.5, color: "var(--text-secondary)", paddingLeft: 92,
              overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap",
            }}>
              {ports.find(p => p.port === selectedPort)?.description ?? ""}
            </div>
          )}

          <div className="form-row">
            <span className="form-label">Baud Rate</span>
            <select className="form-select" value={baud} onChange={e => setBaud(Number(e.target.value))}>
              <option value={9600}>9600</option>
              <option value={115200}>115200</option>
              <option value={230400}>230400</option>
            </select>
          </div>

          <div className="form-btn-row">
            <button className="btn-primary" style={{ flex: 1 }} onClick={connect} disabled={loading || isConn}>
              {loading ? "Connecting..." : "Connect"}
            </button>
            <button className="btn-ghost" style={{ flex: 1 }} onClick={disconnect} disabled={loading || !isConn}>
              Disconnect
            </button>
          </div>

          {msg && (
            <div style={{ fontSize: 12, color: "var(--cyan)", fontFamily: "var(--font-mono)", marginTop: 4 }}>
              {msg}
            </div>
          )}
        </div>

        {/* Status card */}
        <div className="settings-card">
          <div className="section-label">Connection Status</div>
          <div className="status-rows">
            <StatusRow
              label="WebSocket"
              icon={wsConnected ? <Wifi size={15} /> : <WifiOff size={15} />}
              value={wsConnected ? "Connected" : "Disconnected"}
              ok={wsConnected}
            />
            <StatusRow
              label="ESP32 Serial"
              icon={isConn ? <PlugZap size={15} /> : <Unplug size={15} />}
              value={isConn ? `${status?.port}` : "Not connected"}
              ok={isConn}
            />
            {isConn && (
              <>
                <StatusRow label="Baud Rate" value={`${status?.baudRate ?? "—"}`} ok={true} />
                <StatusRow label="RX Throughput" value={`${status?.rxRate?.toFixed(1) ?? "—"} pkt/s`} ok={true} />
              </>
            )}
            <StatusRow
              label="Backend API"
              value={status?.backendOK ? "Running" : "Unreachable"}
              ok={!!status?.backendOK}
            />
          </div>

          <div style={{ marginTop: 14, borderTop: "1px solid var(--border)", paddingTop: 12 }}>
            <div className="section-label">Endpoints</div>
            <div style={{ display: "flex", flexDirection: "column", gap: 6, fontFamily: "var(--font-mono)", fontSize: 11.5, color: "var(--text-secondary)" }}>
              <span>REST  → <strong style={{ color: "var(--cyan)" }}>http://localhost:8000/api</strong></span>
              <span>WS    → <strong style={{ color: "var(--cyan)" }}>ws://localhost:8000/ws/telemetry</strong></span>
              <span>Docs  → <strong style={{ color: "var(--text-primary)" }}>http://localhost:8000/docs</strong></span>
            </div>
          </div>
        </div>
        <TippingCard />
      </div>
    </div>
  );
}

function StatusRow({
  label, icon, value, ok,
}: {
  label: string; icon?: React.ReactNode; value: string; ok: boolean;
}) {
  return (
    <div className="status-row">
      {icon && <span style={{ color: ok ? "var(--green)" : "var(--text-muted)" }}>{icon}</span>}
      <span className="status-key">{label}</span>
      <span className={`status-value ${ok ? "ok" : "err"}`}>{value}</span>
    </div>
  );
}
