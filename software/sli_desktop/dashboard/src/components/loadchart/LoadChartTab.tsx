import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { Plus, Copy, Trash2, Save, Upload, Download, X } from "lucide-react";
import type { LoadChartProfile, TelemetryFrame } from "../../types";
import { CapacityTable } from "./CapacityTable";
import { RangeDiagram, type LivePose } from "./RangeDiagram";
import {
  capacityAt, headroom, validateProfile, deriveProfile, sameChart, zoneFactor,
  DERIVE_PRESETS, LC_MAX_ZONES,
} from "./chartMath";
import { liveSafety, useNow, type ReceivedSafety } from "../sli/sliStatus";

const API = "http://localhost:8000/api";
const BOOM_BASE_KEY = "sli.boomBaseMm";
const BOOM_BASE_DEFAULT = 225;   // firmware config.h BASE_BOOM_LENGTH_MM

interface LoadChartTabProps {
  frame: TelemetryFrame | null;
  safety: ReceivedSafety;
  craneChart: { chart: LoadChartProfile | null; receivedAt: number } | undefined;
  lcResult: { text: string; seq: number };
}

const UPLOAD_TIMEOUT_MS = 4000;

async function call(method: string, path: string, body?: unknown): Promise<{ ok: boolean; data: any }> {
  try {
    const res = await fetch(`${API}${path}`, {
      method,
      headers: body ? { "Content-Type": "application/json" } : undefined,
      body: body ? JSON.stringify(body) : undefined,
    });
    return { ok: res.ok, data: await res.json().catch(() => ({})) };
  } catch {
    return { ok: false, data: { detail: "Backend offline" } };
  }
}

function blankProfile(name: string): LoadChartProfile {
  // Deliberately tiny capacities: a new chart allows almost nothing until real numbers go in
  return {
    name, description: "", anglesDeg: [0, 10, 20], extensionsMM: [0, 45],
    capacityKg: [[0.1, 0.1], [0.1, 0.1], [0.1, 0.1]], zones: [], warnPct: 90, cutPct: 100,
  };
}

function uniqueName(base: string, taken: string[]): string {
  if (!taken.includes(base)) return base;
  for (let n = 2; ; n++) {
    const s = `${base.slice(0, 15 - String(n).length - 1)} ${n}`;
    if (!taken.includes(s)) return s;
  }
}

function readBoomBase(): number {
  try {
    const v = Number(localStorage.getItem(BOOM_BASE_KEY));
    return v > 0 ? v : BOOM_BASE_DEFAULT;
  } catch {
    return BOOM_BASE_DEFAULT;
  }
}

/** Cell (lower corner row/col) the pose is in, or null outside the breakpoints. */
function cellOf(p: LoadChartProfile, deg: number, ext: number): [number, number] | null {
  const idx = (b: number[], x: number) => {
    if (!(x >= b[0] && x <= b[b.length - 1])) return -1;
    for (let i = 0; i + 1 < b.length; i++) if (x <= b[i + 1]) return i;
    return -1;
  };
  const i = idx(p.anglesDeg, deg), j = idx(p.extensionsMM, ext);
  return i >= 0 && j >= 0 ? [i, j] : null;
}

export function LoadChartTab({ frame, safety, craneChart, lcResult }: LoadChartTabProps) {
  const [profiles, setProfiles] = useState<LoadChartProfile[] | null>(null);
  const [selected, setSelected] = useState<string | null>(null);   // saved profile being edited (null = new)
  const [draft, setDraft] = useState<LoadChartProfile | null>(null);
  const [dirty, setDirty] = useState(false);
  const [msg, setMsg] = useState<{ text: string; kind: "ok" | "err" | "info" } | null>(null);
  // Upload in flight: its result is the first LC COMMITTED/ERR after seqBefore
  const [pending, setPending] = useState<{ seqBefore: number; timedOut: boolean } | null>(null);
  const [scalePct, setScalePct] = useState("100");
  const [boomBase, setBoomBase] = useState(readBoomBase);

  const s = liveSafety(safety, useNow());
  const onCrane = s ? s.chart : null;
  const onCraneRef = useRef(onCrane);              // read by load() without re-creating it
  useEffect(() => { onCraneRef.current = onCrane; }, [onCrane]);

  const load = useCallback(async () => {
    const r = await call("GET", "/loadchart/profiles");
    if (!r.ok) {
      setProfiles([]);
      setMsg({ text: r.data.detail ?? "Could not load profiles", kind: "err" });
      return;
    }
    const list = r.data as LoadChartProfile[];
    setProfiles(list);
    // Nothing open yet: open the chart that is on the crane, else the first one
    const pick = list.find(p => p.name === onCraneRef.current) ?? list[0];
    if (pick) {
      setSelected(cur => cur ?? pick.name);
      setDraft(cur => cur ?? structuredClone(pick));
    }
  }, []);

  useEffect(() => { load(); }, [load]);

  // ---- Upload outcome (derived, not copied into state) ----
  const result = pending && lcResult.seq > pending.seqBefore ? lcResult.text : null;
  const uploadMsg = result
    ? result.startsWith("$ACK,LC,COMMITTED")
      ? { text: `On the crane: ${result.split(",").slice(3).join(",")}`, kind: "ok" as const }
      : { text: `ESP32 rejected the chart: ${result.split(",").slice(3).join(",")}`, kind: "err" as const }
    : pending?.timedOut
      ? { text: "No answer from the ESP32 (connected? firmware with SafetyTask?)", kind: "err" as const }
      : null;
  const sending = !!pending && !uploadMsg;
  const committed = !!result && result.startsWith("$ACK,LC,COMMITTED");

  // After a commit, read the chart back to show what the ESP32 really holds
  useEffect(() => {
    if (committed) call("POST", "/loadchart/readback");
  }, [committed]);

  useEffect(() => {
    if (!sending) return;
    const t = setTimeout(() => setPending(p => (p ? { ...p, timedOut: true } : p)), UPLOAD_TIMEOUT_MS);
    return () => clearTimeout(t);
  }, [sending]);

  const say = (m: typeof msg) => { setPending(null); setMsg(m); };
  const edit = (p: LoadChartProfile) => { setDraft(p); setDirty(true); };

  const open = (p: LoadChartProfile) => {
    if (dirty && !window.confirm(`Discard unsaved changes to "${draft?.name}"?`)) return;
    setSelected(p.name);
    setDraft(structuredClone(p));
    setDirty(false);
    say(null);
  };

  const startNew = (p: LoadChartProfile) => {
    if (dirty && !window.confirm(`Discard unsaved changes to "${draft?.name}"?`)) return;
    setSelected(null);
    setDraft(p);
    setDirty(true);
    say({ text: "Not saved yet", kind: "info" });
  };

  const names = (profiles ?? []).map(p => p.name);
  const error = draft ? validateProfile(draft) : null;

  const save = async () => {
    if (!draft || error) return;
    if (selected !== draft.name && names.includes(draft.name) &&
        !window.confirm(`A profile named "${draft.name}" exists. Overwrite it?`)) return;
    const r = await call("PUT", `/loadchart/profiles/${encodeURIComponent(draft.name)}`, draft);
    if (!r.ok) { say({ text: r.data.detail ?? "Save failed", kind: "err" }); return; }
    setSelected(draft.name);
    setDraft(r.data as LoadChartProfile);
    setDirty(false);
    say({ text: "Saved", kind: "ok" });
    load();
  };

  const remove = async (name: string) => {
    if (!window.confirm(`Delete profile "${name}"? The file is removed from the PC (the chart on the crane stays).`)) return;
    const r = await call("DELETE", `/loadchart/profiles/${encodeURIComponent(name)}`);
    if (!r.ok) { say({ text: r.data.detail ?? "Delete failed", kind: "err" }); return; }
    if (selected === name) { setSelected(null); setDraft(null); setDirty(false); }
    load();
  };

  const upload = async () => {
    if (!draft || dirty || !selected) return;
    say({ text: `Sending ${selected}…`, kind: "info" });
    const seqBefore = lcResult.seq;
    const r = await call("POST", `/loadchart/profiles/${encodeURIComponent(selected)}/upload`);
    if (!r.ok) { say({ text: r.data.detail ?? "Upload failed", kind: "err" }); return; }
    setPending({ seqBefore, timedOut: false });
  };

  const readback = async () => {
    const r = await call("POST", "/loadchart/readback");
    say(r.ok ? { text: "Asked the ESP32 for its chart", kind: "info" }
             : { text: r.data.detail ?? "Readback failed", kind: "err" });
  };

  const applyScale = () => {
    const f = Number(scalePct) / 100;
    if (!draft || !(f > 0 && f <= 1.5)) { say({ text: "Scale must be 1–150 %", kind: "err" }); return; }
    edit({ ...draft, capacityKg: draft.capacityKg.map(r => r.map(v => Math.round(v * f * 1000) / 1000)) });
    setScalePct("100");
  };

  const setBase = (v: number) => {
    setBoomBase(v);
    try { localStorage.setItem(BOOM_BASE_KEY, String(v)); } catch { /* private window */ }
  };

  // ---- Live pose + readouts ----
  const pose: LivePose | null = frame
    ? { boomDeg: frame.boomAngle, extMm: frame.extensionMM, swingDeg: frame.swingAngle, loadKg: frame.actualLoad }
    : null;
  const valid = !!draft && !error;
  const here = valid && pose ? capacityAt(draft!, pose.boomDeg, pose.extMm, pose.swingDeg) : null;
  const room = valid && pose ? headroom(draft!, pose.boomDeg, pose.extMm, pose.swingDeg, pose.loadKg) : null;
  const liveCell = valid && pose ? cellOf(draft!, pose.boomDeg, pose.extMm) : null;

  const crane = craneChart?.chart ?? null;
  const craneMatches = useMemo(() => !!draft && !!crane && sameChart(draft, crane), [draft, crane]);

  return (
    <div className="lc-page">
      {/* ===== Profiles ===== */}
      <div className="panel lc-profiles">
        <div className="panel-header">
          <span className="ph-icon">◈</span> Profiles
          <button className="btn-ghost lc-hdr-btn" title="New blank chart"
                  onClick={() => startNew(blankProfile(uniqueName("New chart", names)))}>
            <Plus size={11} /> New
          </button>
        </div>
        <div className="panel-body lc-prof-list">
          {profiles === null && <div className="lc-empty">Loading…</div>}
          {profiles?.length === 0 && !draft && (
            <div className="lc-empty">
              No profiles yet. Start with <b>New</b>, fill in the limits you measured on the crane,
              then derive the working profiles from it.
            </div>
          )}
          {profiles?.map(p => (
            <div key={p.name} className={`lc-prof${selected === p.name ? " sel" : ""}`}
                 role="button" tabIndex={0} onClick={() => open(p)}
                 onKeyDown={e => { if (e.key === "Enter") open(p); }}>
              <div className="lc-prof-top">
                <span className="lc-prof-name">{p.name}</span>
                {onCrane === p.name && <span className="lc-stamp">ON CRANE</span>}
              </div>
              <div className="lc-prof-sub">
                {p.anglesDeg.length}×{p.extensionsMM.length}
                {p.zones.length > 0 && ` · ${p.zones.length} zone${p.zones.length > 1 ? "s" : ""}`}
                {` · cut ${p.cutPct} %`}
              </div>
              {p.description && <div className="lc-prof-desc" title={p.description}>{p.description}</div>}
              <button className="lc-prof-del" title={`Delete ${p.name}`}
                      onClick={e => { e.stopPropagation(); remove(p.name); }}>
                <Trash2 size={11} />
              </button>
            </div>
          ))}
          {draft && !selected && (
            <div className="lc-prof sel">
              <div className="lc-prof-top"><span className="lc-prof-name">{draft.name || "(no name)"}</span></div>
              <div className="lc-prof-sub">unsaved</div>
            </div>
          )}
        </div>

        {draft && (
          <div className="lc-derive">
            <div className="tele-section-title"><span>Derive from {draft.name || "this"}</span></div>
            <div className="lc-derive-note">Base should hold the limits you measured (tipping / stall).</div>
            {DERIVE_PRESETS.map(d => (
              <button key={d.key} className="btn-ghost lc-derive-btn" title={d.hint} disabled={!valid}
                      onClick={() => startNew(deriveProfile(draft, d.key, d.factor))}>
                {d.label} <span>{Math.round(d.factor * 100)} %</span>
              </button>
            ))}
            <button className="btn-ghost lc-derive-btn" disabled={!valid}
                    onClick={() => startNew({ ...structuredClone(draft), name: uniqueName(draft.name.slice(0, 13) + " c", names), updated: null })}>
              <Copy size={10} /> Duplicate
            </button>
          </div>
        )}
      </div>

      {/* ===== Editor ===== */}
      <div className="panel lc-editor">
        <div className="panel-header">
          <span className="ph-icon">◈</span> Rated capacity chart
          {dirty && <span className="lc-dirty" title="Unsaved changes">● unsaved</span>}
        </div>
        {!draft ? (
          <div className="lc-empty" style={{ margin: 16 }}>Pick a profile or create a new one.</div>
        ) : (
          <div className="panel-body lc-editor-body">
            <div className="lc-row">
              <label className="lc-field grow">
                <span>Name</span>
                <input className="tele-input" maxLength={15} value={draft.name}
                       onChange={e => edit({ ...draft, name: e.target.value })} />
              </label>
              <label className="lc-field">
                <span>Warn %</span>
                <input className="tele-input lc-num" type="number" value={draft.warnPct}
                       onChange={e => edit({ ...draft, warnPct: Number(e.target.value) })} />
              </label>
              <label className="lc-field">
                <span>Cut %</span>
                <input className="tele-input lc-num" type="number" value={draft.cutPct}
                       onChange={e => edit({ ...draft, cutPct: Number(e.target.value) })} />
              </label>
            </div>
            <label className="lc-field">
              <span>Description</span>
              <input className="tele-input lc-wide" maxLength={120} value={draft.description}
                     placeholder="What this profile is for, where the numbers came from"
                     onChange={e => edit({ ...draft, description: e.target.value })} />
            </label>

            <CapacityTable draft={draft} onChange={edit} liveCell={liveCell} crane={crane} />

            <div className="lc-row">
              <span className="lc-k">Scale all cells</span>
              <input className="tele-input lc-num" type="number" value={scalePct}
                     onChange={e => setScalePct(e.target.value)} />
              <span className="lc-k">%</span>
              <button className="btn-ghost" onClick={applyScale}>Apply</button>
            </div>

            {/* Swing zones */}
            <div className="tele-section-card">
              <div className="tele-section-title">
                <span>Swing zones (capacity × factor)</span>
                <button className="btn-ghost" style={{ padding: "1px 6px", fontSize: 9 }}
                        disabled={draft.zones.length >= LC_MAX_ZONES}
                        onClick={() => edit({ ...draft, zones: [...draft.zones, { fromDeg: 135, toDeg: 225, factor: 1 }] })}>
                  <Plus size={9} /> Zone
                </button>
              </div>
              {draft.zones.length === 0 && <div className="lc-k">None: the chart applies all the way round.</div>}
              {draft.zones.map((z, i) => {
                const setZ = (patch: Partial<typeof z>) =>
                  edit({ ...draft, zones: draft.zones.map((q, k) => (k === i ? { ...q, ...patch } : q)) });
                return (
                  <div key={i} className="tele-input-row">
                    <span className="lc-k">Zone {i + 1}</span>
                    <input className="tele-input lc-num" type="number" value={z.fromDeg} aria-label="From (deg)"
                           onChange={e => setZ({ fromDeg: Number(e.target.value) })} />
                    <span className="lc-k">° to</span>
                    <input className="tele-input lc-num" type="number" value={z.toDeg} aria-label="To (deg)"
                           onChange={e => setZ({ toDeg: Number(e.target.value) })} />
                    <span className="lc-k">° ×</span>
                    <input className="tele-input lc-num" type="number" step="0.05" value={z.factor} aria-label="Factor"
                           onChange={e => setZ({ factor: Number(e.target.value) })} />
                    <button className="lc-mini" title="Remove zone"
                            onClick={() => edit({ ...draft, zones: draft.zones.filter((_, k) => k !== i) })}>
                      <X size={10} />
                    </button>
                  </div>
                );
              })}
            </div>

            {error && <div className="lc-error">⚠ {error}</div>}

            <div className="lc-actions">
              <button className="btn-primary" onClick={save} disabled={!!error || !dirty}>
                <Save size={11} /> Save
              </button>
              <button className="btn-primary" onClick={upload} disabled={dirty || !selected || !!error || sending}
                      title={dirty ? "Save first: the upload sends the saved profile" : "Send to the ESP32 (replaces its chart)"}>
                <Upload size={11} /> {sending ? "Sending…" : "Upload to crane"}
              </button>
              <button className="btn-ghost" onClick={readback} title="Read the chart the ESP32 is enforcing">
                <Download size={11} /> Read from crane
              </button>
              {craneChart && (
                <span className={`lc-compare ${crane ? (craneMatches ? "ok" : "diff") : "none"}`}>
                  {!crane ? "Crane has no chart" : craneMatches ? "Matches the crane" : `Crane runs "${crane.name}" (differs)`}
                </span>
              )}
            </div>
            {(uploadMsg ?? msg) && (
              <div className={`lc-msg ${(uploadMsg ?? msg)!.kind}`}>{(uploadMsg ?? msg)!.text}</div>
            )}
          </div>
        )}
      </div>

      {/* ===== Diagram + live ===== */}
      <div className="lc-right">
        <div className="panel lc-diagram-panel">
          <div className="panel-header">
            <span className="ph-icon">◈</span> Range diagram
            <label className="lc-base" title="Boom length from pivot to tip at 0 mm extension. Drawing only: the SLI uses angle × extension.">
              boom @0 mm
              <input className="tele-input lc-num" type="number" value={boomBase}
                     onChange={e => { const v = Number(e.target.value); if (v > 0) setBase(v); }} />
              mm
            </label>
          </div>
          {valid ? (
            <RangeDiagram profile={draft!} boomBaseMm={boomBase} live={pose}
                          liveState={s ? s.state : null} />
          ) : (
            <div className="lc-empty" style={{ margin: 16 }}>
              {draft ? "Fix the chart to see the diagram." : "No chart open."}
            </div>
          )}
        </div>

        <div className="panel lc-live">
          <div className="panel-header"><span className="ph-icon">◈</span> Live</div>
          <div className="lc-live-grid">
            <div>
              <div className="tele-k">SLI on the crane</div>
              <div className={`lc-live-state ${s ? s.state.toLowerCase() : "stale"}`}>
                {s ? (s.state === "ESTOP" ? "E-STOP" : s.state) : "NO SIGNAL"}
              </div>
              <div className="lc-k">
                {s ? `${s.pct.toFixed(1)} % of ${s.capKg.toFixed(3)} kg · ${s.chart}` : "no $S from the ESP32"}
              </div>
            </div>
            <div>
              <div className="tele-k">Load</div>
              <div className="lc-live-v">{pose ? `${pose.loadKg.toFixed(3)} kg` : "–"}</div>
              <div className="lc-k">{pose ? `${pose.boomDeg.toFixed(1)}° · ${pose.extMm.toFixed(1)} mm · swing ${pose.swingDeg.toFixed(0)}°` : "no telemetry"}</div>
            </div>
            <div>
              <div className="tele-k">This profile here</div>
              <div className="lc-live-v">
                {here === null ? "–" : here > 0 ? `${here.toFixed(3)} kg` : "outside chart"}
              </div>
              <div className="lc-k">
                {here && pose ? `${(Math.max(0, pose.loadKg) / here * 100).toFixed(1)} %` : ""}
                {draft && pose && zoneFactor(draft, pose.swingDeg)[1] >= 0 ? ` · zone ×${zoneFactor(draft, pose.swingDeg)[0]}` : ""}
              </div>
            </div>
            <div>
              <div className="tele-k">Headroom before cut</div>
              {room === null ? <div className="lc-live-v">–</div>
                : room.boomDownDeg === null ? <div className="lc-live-v" style={{ color: "var(--red)" }}>at limit</div>
                : (
                  <>
                    <div className="lc-live-v">↓ {room.boomDownDeg.toFixed(1)}°</div>
                    <div className="lc-k">extend {room.extendMm!.toFixed(0)} mm more</div>
                  </>
                )}
            </div>
          </div>
          {draft && onCrane && onCrane !== draft.name && (
            <div className="lc-k" style={{ padding: "0 12px 10px" }}>
              Previewing "{draft.name}". The crane enforces "{onCrane}".
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
