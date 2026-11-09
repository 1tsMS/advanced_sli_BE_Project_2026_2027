import { useEffect, useState, type RefObject } from "react";
import { Radio, History, Trash2, Pencil, Check, X, LayoutGrid, Rows3, Columns2 } from "lucide-react";
import type { LivePoint, LiftSummary, LiftReport, Capture } from "../../types";
import { LiveCharts } from "./LiveCharts";
import { LiftReview } from "./LiftReview";
import { fmtS, CHART_LAYOUTS, ChartLayoutContext, type ChartLayout } from "./chartKit";
import "./datalogger.css";
import { API_BASE } from "../../api";


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
export function liftDate(l: { id: string; startedAt?: string | null }): string {
  const d = l.startedAt ? new Date(l.startedAt) : null;
  if (!d || isNaN(+d)) return l.id;
  return d.toLocaleString(undefined, { day: "2-digit", month: "short", hour: "2-digit", minute: "2-digit" });
}

/** The lift's name if it has one, else its date. */
export function liftLabel(l: { id: string; name?: string; startedAt?: string | null }): string {
  return l.name ? l.name : liftDate(l);
}

/** PATCH the name / note of a saved lift. */
async function renameLift(id: string, name: string, note: string): Promise<boolean> {
  try {
    const r = await fetch(`${API_BASE}/lifts/${id}`, {
      method: "PATCH", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ name, note }),
    });
    return r.ok;
  } catch { return false; }
}

const LAYOUT_KEY = "sli_chart_layout";
const LAYOUT_ICON: Record<ChartLayout, React.ReactNode> = {
  default: <Columns2 size={15} />, overview: <LayoutGrid size={15} />, detail: <Rows3 size={15} />,
};
function storedLayout(): ChartLayout {
  try {
    const v = localStorage.getItem(LAYOUT_KEY);
    if (v === "default" || v === "overview" || v === "detail") return v;
  } catch { /* blocked */ }
  return "default";
}

/**
 * Data Logger: Live (rolling 60 s from the WebSocket) and Lifts (recorded with
 * Start / Stop Lift, analysed by the backend when the lift stops).
 */
export function DataLoggerTab({ history, capture, onStartCapture, onStopCapture, selectedLift, onSelectLift, liftsVersion, warnPct, cutPct }: {
  history: RefObject<LivePoint[]>;
  capture: RefObject<Capture>;
  onStartCapture: () => void;
  onStopCapture: () => void;
  selectedLift: string | null;
  onSelectLift: (id: string | null) => void;
  liftsVersion: number;              // bumps when a lift stops, to refresh the list
  warnPct?: number; cutPct?: number;
}) {
  const [mode, setMode] = useState<"live" | "lifts">(selectedLift ? "lifts" : "live");
  const [lifts, setLifts] = useState<LiftSummary[]>([]);
  const [report, setReport] = useState<LiftReport | null>(null);
  const [layout, setLayout] = useState<ChartLayout>(storedLayout);
  const pickLayout = (l: ChartLayout) => {
    setLayout(l);
    try { localStorage.setItem(LAYOUT_KEY, l); } catch { /* not remembered */ }
  };

  useEffect(() => { fetchLifts().then(setLifts); }, [liftsVersion]);
  useEffect(() => { if (selectedLift) setMode("lifts"); }, [selectedLift]);
  useEffect(() => {
    if (!selectedLift && lifts.length && mode === "lifts") onSelectLift(lifts[0].id);
  }, [lifts, selectedLift, mode, onSelectLift]);
  useEffect(() => {
    setReport(null);
    if (selectedLift) fetchReport(selectedLift).then(setReport);
  }, [selectedLift, liftsVersion]);

  // Inline rename: one lift at a time
  const [editing, setEditing] = useState<string | null>(null);
  const [draftName, setDraftName] = useState("");
  const [draftNote, setDraftNote] = useState("");
  const [renameErr, setRenameErr] = useState(false);
  const startEdit = (l: LiftSummary) => {
    setEditing(l.id); setDraftName(l.name ?? ""); setDraftNote(l.note ?? ""); setRenameErr(false);
  };
  const saveEdit = async () => {
    if (!editing) return;
    if (await renameLift(editing, draftName, draftNote)) {
      setEditing(null);
      fetchLifts().then(setLifts);
    } else setRenameErr(true);
  };

  const remove = async (id: string) => {
    if (!window.confirm(`Delete ${id}? Its CSV and report are removed from the logs folder for good.`)) return;
    await fetch(`${API_BASE}/lifts/${id}`, { method: "DELETE" }).catch(() => null);
    if (selectedLift === id) onSelectLift(null);
    fetchLifts().then(setLifts);
  };

  return (
    <ChartLayoutContext.Provider value={layout}>
    <div className={`dl-page ${layout === "overview" ? "fit" : ""}`}>
      <div className="dl-topbar">
        <div className="dl-seg" role="tablist" aria-label="Data source">
          <button className={mode === "live" ? "on" : ""} onClick={() => setMode("live")}><Radio size={15} /> Live</button>
          <button className={mode === "lifts" ? "on" : ""} onClick={() => setMode("lifts")}><History size={15} /> Lifts ({lifts.length})</button>
        </div>
        <span className="dl-muted">
          {mode === "live" ? "Last 60 s. Start a lift in the header to record and analyse one."
                           : "Every chart exports as PNG (white) or SVG for the report."}
        </span>
        <span className="dl-card-fill" />
        <span className="dl-label">Layout</span>
        <div className="dl-seg" role="radiogroup" aria-label="Chart layout">
          {CHART_LAYOUTS.map(l => (
            <button key={l.id} className={layout === l.id ? "on" : ""} onClick={() => pickLayout(l.id)}
                    title={l.hint} role="radio" aria-checked={layout === l.id}>
              {LAYOUT_ICON[l.id]} {l.label}
            </button>
          ))}
        </div>
      </div>

      {mode === "live" && <LiveCharts history={history} capture={capture} onStartCapture={onStartCapture}
                                      onStopCapture={onStopCapture} warnPct={warnPct} cutPct={cutPct} />}

      {mode === "lifts" && (
        <div className="dl-lifts">
          <div className="dl-liftlist">
            {lifts.length === 0 && <div className="dl-empty small">No lifts yet. Pick an operator in the header and press Start lift.</div>}
            {lifts.map(l => (
              <div key={l.id} className={`dl-liftitem ${l.id === selectedLift ? "on" : ""}`} onClick={() => onSelectLift(l.id)}>
                {editing === l.id ? (
                  <div className="dl-rename" onClick={e => e.stopPropagation()}
                       onKeyDown={e => { if (e.key === "Escape") setEditing(null); }}>
                    <input autoFocus value={draftName} maxLength={80} placeholder="Name, e.g. E1-idle-1"
                           aria-label="Lift name" onChange={e => setDraftName(e.target.value)}
                           onKeyDown={e => { if (e.key === "Enter") saveEdit(); }} />
                    <textarea value={draftNote} maxLength={200} rows={2} placeholder="Note (setup, what changed)"
                              aria-label="Lift note" onChange={e => setDraftNote(e.target.value)} />
                    <div className="dl-rename-row">
                      {renameErr && <span className="dl-rename-err">Not saved (backend restarted?)</span>}
                      <span className="dl-card-fill" />
                      <button className="dl-icon-btn" title="Cancel (Esc)" onClick={() => setEditing(null)}><X size={13} /></button>
                      <button className="dl-icon-btn" title="Save (Enter)" onClick={saveEdit}><Check size={13} /></button>
                    </div>
                  </div>
                ) : (
                  <>
                    <div className="dl-liftitem-top">
                      <b className="dl-liftname">{l.name || liftDate(l)}</b>
                      {l.grade && <span className={`dl-grade g${l.grade}`}>{l.grade}</span>}
                      <span className="dl-card-fill" />
                      <button className="dl-del dl-edit" title="Rename / note" onClick={e => { e.stopPropagation(); startEdit(l); }}>
                        <Pencil size={11} />
                      </button>
                      <button className="dl-del" title="Delete this lift" onClick={e => { e.stopPropagation(); remove(l.id); }}>
                        <Trash2 size={11} />
                      </button>
                    </div>
                    <div className="dl-muted">
                      {l.operator ?? "?"} · {l.name ? `${liftDate(l)} · ` : ""}{l.durationS !== null ? fmtS(l.durationS) : "—"}
                      {l.liftedKg ? ` · ${(l.liftedKg * 1000).toFixed(0)} g` : ""}
                    </div>
                    {l.note && <div className="dl-muted dl-note" title={l.note}>{l.note}</div>}
                  </>
                )}
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
    </ChartLayoutContext.Provider>
  );
}
