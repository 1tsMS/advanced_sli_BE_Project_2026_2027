import { useEffect, useState, type RefObject } from "react";
import { Radio, History, Trash2 } from "lucide-react";
import type { LivePoint, LiftSummary, LiftReport } from "../../types";
import { LiveCharts } from "./LiveCharts";
import { LiftReview } from "./LiftReview";
import { fmtS } from "./chartKit";
import "./datalogger.css";

const API_BASE = "http://localhost:8000/api";

export async function fetchLifts(): Promise<LiftSummary[]> {
  try {
    const r = await fetch(`${API_BASE}/lifts`);
    return r.ok ? (await r.json()).lifts : [];
  } catch { return []; }
}

export async function fetchReport(id: string): Promise<LiftReport | null> {
  try {
    const r = await fetch(`${API_BASE}/lifts/${id}`);
    return r.ok ? await r.json() : null;
  } catch { return null; }
}

/** "lift_20261102_143005" -> "02 Nov 14:30" */
export function liftLabel(l: { id: string; startedAt?: string | null }): string {
  const d = l.startedAt ? new Date(l.startedAt) : null;
  if (!d || isNaN(+d)) return l.id;
  return d.toLocaleString(undefined, { day: "2-digit", month: "short", hour: "2-digit", minute: "2-digit" });
}

/**
 * Data Logger: Live (rolling 60 s from the WebSocket) and Lifts (recorded with
 * Start / Stop Lift, analysed by the backend when the lift stops).
 */
export function DataLoggerTab({ history, selectedLift, onSelectLift, liftsVersion, warnPct, cutPct }: {
  history: RefObject<LivePoint[]>;
  selectedLift: string | null;
  onSelectLift: (id: string | null) => void;
  liftsVersion: number;              // bumps when a lift stops, to refresh the list
  warnPct?: number; cutPct?: number;
}) {
  const [mode, setMode] = useState<"live" | "lifts">(selectedLift ? "lifts" : "live");
  const [lifts, setLifts] = useState<LiftSummary[]>([]);
  const [report, setReport] = useState<LiftReport | null>(null);

  useEffect(() => { fetchLifts().then(setLifts); }, [liftsVersion]);
  useEffect(() => { if (selectedLift) setMode("lifts"); }, [selectedLift]);
  useEffect(() => {
    if (!selectedLift && lifts.length && mode === "lifts") onSelectLift(lifts[0].id);
  }, [lifts, selectedLift, mode, onSelectLift]);
  useEffect(() => {
    setReport(null);
    if (selectedLift) fetchReport(selectedLift).then(setReport);
  }, [selectedLift, liftsVersion]);

  const remove = async (id: string) => {
    if (!window.confirm(`Delete ${id}? Its CSV and report are removed from the logs folder for good.`)) return;
    await fetch(`${API_BASE}/lifts/${id}`, { method: "DELETE" }).catch(() => null);
    if (selectedLift === id) onSelectLift(null);
    fetchLifts().then(setLifts);
  };

  return (
    <div className="dl-page">
      <div className="dl-topbar">
        <div className="dl-seg" role="tablist">
          <button className={mode === "live" ? "on" : ""} onClick={() => setMode("live")}><Radio size={12} /> Live</button>
          <button className={mode === "lifts" ? "on" : ""} onClick={() => setMode("lifts")}><History size={12} /> Lifts ({lifts.length})</button>
        </div>
        <span className="dl-muted">
          {mode === "live" ? "Last 60 s of telemetry. Start a lift from the header to record and analyse it."
                           : "Recorded lifts. Every chart exports as PNG (white, for the report) or SVG."}
        </span>
      </div>

      {mode === "live" && <LiveCharts history={history} warnPct={warnPct} cutPct={cutPct} />}

      {mode === "lifts" && (
        <div className="dl-lifts">
          <div className="dl-liftlist">
            {lifts.length === 0 && <div className="dl-empty small">No lifts yet. Pick an operator in the header and press Start lift.</div>}
            {lifts.map(l => (
              <div key={l.id} className={`dl-liftitem ${l.id === selectedLift ? "on" : ""}`} onClick={() => onSelectLift(l.id)}>
                <div className="dl-liftitem-top">
                  <b>{l.operator ?? "?"}</b>
                  {l.grade && <span className={`dl-grade g${l.grade}`}>{l.grade}</span>}
                  <span className="dl-card-fill" />
                  <button className="dl-del" title="Delete this lift" onClick={e => { e.stopPropagation(); remove(l.id); }}>
                    <Trash2 size={11} />
                  </button>
                </div>
                <div className="dl-muted">
                  {liftLabel(l)} · {l.durationS !== null ? fmtS(l.durationS) : "—"}
                  {l.liftedKg ? ` · ${(l.liftedKg * 1000).toFixed(0)} g` : ""}
                </div>
                {l.note && <div className="dl-muted dl-note">{l.note}</div>}
              </div>
            ))}
          </div>
          <div className="dl-liftmain">
            {selectedLift
              ? (report && !report.ok
                  ? <div className="dl-empty">{report.reason ?? "This lift could not be analysed."}</div>
                  : <LiftReview liftId={selectedLift} report={report} />)
              : <div className="dl-empty">Select a lift.</div>}
          </div>
        </div>
      )}
    </div>
  );
}
