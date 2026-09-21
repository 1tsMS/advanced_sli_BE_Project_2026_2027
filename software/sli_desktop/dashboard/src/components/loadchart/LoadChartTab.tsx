import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { Plus, Trash2, Upload, Download } from "lucide-react";
import { apiJson, type ApiResult } from "../../api";
import type { LoadChartRow } from "../../types";

// Must match the limits in firmware load_chart.h and backend models.py
const MAX_ROWS = 32;
const MAX_ANGLE = 90;
const MAX_EXT_MM = 2000;
const MAX_LIMIT_KG = 100;

/** Editing state keeps raw text so half-typed numbers don't get mangled. */
interface EditRow { id: number; angle: string; ext: string; limit: string; }

interface Status { kind: "ok" | "err" | "info"; text: string; }

let nextId = 1;
const newRow = (r?: Partial<LoadChartRow>): EditRow => ({
  id: nextId++,
  angle: r?.angle !== undefined ? String(r.angle) : "",
  ext:   r?.extensionMM !== undefined ? String(r.extensionMM) : "",
  limit: r?.limitKg !== undefined ? String(r.limitKg) : "",
});

/** Parse a cell; returns NaN for empty/invalid text. */
const num = (s: string) => (s.trim() === "" ? NaN : Number(s));

function cellError(value: number, max: number): boolean {
  return !Number.isFinite(value) || value < 0 || value > max;
}

export function LoadChartTab({ connected }: { connected: boolean }) {
  const [rows, setRows] = useState<EditRow[]>([newRow()]);
  const [status, setStatus] = useState<Status | null>(null);
  const [busy, setBusy] = useState(false);
  const [dirty, setDirty] = useState(false);

  const autoLoaded = useRef(false);

  /** Put a read-back result into the table and status line. */
  const applyLoaded = useCallback((res: ApiResult<{ entries: LoadChartRow[] }>) => {
    if (!res.ok || !res.data) {
      setStatus({ kind: "err", text: res.error ?? "Could not read the chart" });
      return;
    }
    const entries = res.data.entries;
    setRows(entries.length ? entries.map(e => newRow(e)) : [newRow()]);
    setDirty(false);
    setStatus({
      kind: "ok",
      text: entries.length
        ? `Loaded ${entries.length} entries from ESP32 flash.`
        : "The ESP32 has no saved chart yet.",
    });
  }, []);

  const loadFromEsp = useCallback(async () => {
    setBusy(true);
    setStatus({ kind: "info", text: "Reading chart from ESP32…" });
    const res = await apiJson<{ entries: LoadChartRow[] }>("/loadchart");
    setBusy(false);
    applyLoaded(res);
  }, [applyLoaded]);

  // Load the stored chart once, the first time the ESP32 is connected while this
  // tab is open. Not repeated on reconnects, so unsaved edits are never overwritten.
  useEffect(() => {
    if (!connected || autoLoaded.current) return;
    autoLoaded.current = true;
    apiJson<{ entries: LoadChartRow[] }>("/loadchart").then(applyLoaded);
  }, [connected, applyLoaded]);

  // ---- Validation ----
  const parsed = useMemo(() => rows.map(r => ({
    id: r.id,
    angle: num(r.angle), ext: num(r.ext), limit: num(r.limit),
  })), [rows]);

  const cellErrors = useMemo(() => parsed.map(p => ({
    angle: cellError(p.angle, MAX_ANGLE),
    ext:   cellError(p.ext, MAX_EXT_MM),
    limit: cellError(p.limit, MAX_LIMIT_KG),
  })), [parsed]);

  const hasCellError = cellErrors.some(e => e.angle || e.ext || e.limit);

  const duplicateIds = useMemo(() => {
    const seen = new Map<string, number>();
    const dups = new Set<number>();
    parsed.forEach(p => {
      if (Number.isNaN(p.angle) || Number.isNaN(p.ext)) return;
      const key = `${p.angle}|${p.ext}`;
      if (seen.has(key)) { dups.add(p.id); dups.add(seen.get(key)!); } else seen.set(key, p.id);
    });
    return dups;
  }, [parsed]);

  /**
   * A crane can lift LESS the lower the boom and the further it is extended.
   * Flag pairs where the "worse" position (lower or equal angle, more or equal
   * extension) is allowed MORE load — usually a typo, and unsafe if real.
   */
  const monotonicWarnings = useMemo(() => {
    const out: string[] = [];
    const ok = parsed.filter(p => [p.angle, p.ext, p.limit].every(Number.isFinite));
    for (const p of ok) {
      for (const q of ok) {
        if (p === q) continue;
        const qWorse = q.angle <= p.angle && q.ext >= p.ext && (q.angle < p.angle || q.ext > p.ext);
        if (qWorse && q.limit > p.limit) {
          out.push(
            `${q.angle}° / ${q.ext} mm allows ${q.limit} kg, more than the safer ${p.angle}° / ${p.ext} mm (${p.limit} kg)`
          );
        }
      }
    }
    return out.slice(0, 3);
  }, [parsed]);

  const canUpload = connected && !busy && rows.length > 0 && !hasCellError && duplicateIds.size === 0;

  // ---- Edit handlers ----
  const update = (id: number, field: "angle" | "ext" | "limit", value: string) => {
    setRows(rs => rs.map(r => (r.id === id ? { ...r, [field]: value } : r)));
    setDirty(true);
  };
  const addRow = () => { setRows(rs => [...rs, newRow()]); setDirty(true); };
  const removeRow = (id: number) => {
    setRows(rs => (rs.length > 1 ? rs.filter(r => r.id !== id) : [newRow()]));
    setDirty(true);
  };

  const upload = async () => {
    setBusy(true);
    setStatus({ kind: "info", text: "Uploading and saving to ESP32 flash…" });
    const res = await apiJson<{ message: string }>("/loadchart/upload", {
      body: {
        entries: parsed.map(p => ({ angle: p.angle, extensionMM: p.ext, limitKg: p.limit })),
      },
    });
    setBusy(false);
    if (res.ok) {
      setDirty(false);
      setStatus({ kind: "ok", text: res.data?.message ?? "Load chart saved." });
    } else {
      setStatus({ kind: "err", text: res.error ?? "Upload failed" });
    }
  };

  const inputStyle = (bad: boolean): React.CSSProperties => ({
    width: "100%",
    borderColor: bad ? "var(--red)" : undefined,
  });

  const statusColor = status?.kind === "ok" ? "var(--green)" : status?.kind === "err" ? "var(--red)" : "var(--cyan)";

  return (
    <div style={{ flex: 1, overflowY: "auto", padding: 10 }}>
      <div className="debug-card" style={{ maxWidth: 760 }}>
        <div className="debug-card-header">
          <span className="debug-card-title">Load chart — safe working load by boom angle and extension</span>
          <span className="dim-text" style={{ fontSize: 10.5 }}>{rows.length}/{MAX_ROWS} rows</span>
        </div>

        <div className="debug-card-body" style={{ overflow: "visible" }}>
          <table className="scan-table">
            <thead>
              <tr>
                <th>Boom angle (°)</th>
                <th>Extension (mm)</th>
                <th>Limit (kg)</th>
                <th style={{ width: 36 }} />
              </tr>
            </thead>
            <tbody>
              {rows.map((r, i) => (
                <tr key={r.id} style={duplicateIds.has(r.id) ? { background: "var(--red-dim)" } : undefined}>
                  <td>
                    <input className="tele-input" inputMode="decimal" style={inputStyle(cellErrors[i].angle)}
                           value={r.angle} placeholder={`0–${MAX_ANGLE}`}
                           onChange={e => update(r.id, "angle", e.target.value)} />
                  </td>
                  <td>
                    <input className="tele-input" inputMode="decimal" style={inputStyle(cellErrors[i].ext)}
                           value={r.ext} placeholder={`0–${MAX_EXT_MM}`}
                           onChange={e => update(r.id, "ext", e.target.value)} />
                  </td>
                  <td>
                    <input className="tele-input" inputMode="decimal" style={inputStyle(cellErrors[i].limit)}
                           value={r.limit} placeholder={`0–${MAX_LIMIT_KG}`}
                           onChange={e => update(r.id, "limit", e.target.value)} />
                  </td>
                  <td>
                    <button className="btn-ghost" title="Remove row" onClick={() => removeRow(r.id)}
                            style={{ padding: "3px 6px" }}>
                      <Trash2 size={11} />
                    </button>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>

          {duplicateIds.size > 0 && (
            <div className="fail-text" style={{ fontSize: 11, marginTop: 8 }}>
              Highlighted rows repeat the same angle and extension. Each position may appear only once.
            </div>
          )}
          {monotonicWarnings.length > 0 && (
            <div style={{ fontSize: 11, marginTop: 8, color: "var(--amber)" }}>
              ⚠ Check these values (a lower boom or longer reach should never allow more load):
              <ul style={{ margin: "4px 0 0 16px" }}>
                {monotonicWarnings.map(w => <li key={w}>{w}</li>)}
              </ul>
            </div>
          )}

          <div style={{ display: "flex", gap: 8, marginTop: 12, alignItems: "center", flexWrap: "wrap" }}>
            <button className="btn-ghost" onClick={addRow} disabled={rows.length >= MAX_ROWS}
                    style={{ display: "flex", alignItems: "center", gap: 5 }}>
              <Plus size={11} /> Add row
            </button>
            <button className="btn-ghost" onClick={loadFromEsp} disabled={!connected || busy}
                    title="Replace the table with the chart stored on the ESP32"
                    style={{ display: "flex", alignItems: "center", gap: 5 }}>
              <Download size={11} /> Load from ESP32
            </button>
            <span style={{ flex: 1 }} />
            {dirty && <span className="dim-text" style={{ fontSize: 10.5 }}>Unsaved changes</span>}
            <button className="btn-primary" onClick={upload} disabled={!canUpload}
                    title={connected ? "Send to the ESP32 and save to flash" : "Connect the ESP32 first"}
                    style={{ display: "flex", alignItems: "center", gap: 5 }}>
              <Upload size={11} /> Upload &amp; save
            </button>
          </div>

          {!connected && (
            <div className="dim-text" style={{ fontSize: 11, marginTop: 8 }}>
              Not connected to the ESP32. Connect in Settings to load or save the chart.
            </div>
          )}
          {status && (
            <div style={{ fontSize: 11.5, marginTop: 8, color: statusColor, fontFamily: "var(--font-mono)" }}>
              {status.text}
            </div>
          )}
        </div>
      </div>

      <div className="dim-text" style={{ fontSize: 11, marginTop: 10, maxWidth: 760, lineHeight: 1.6 }}>
        <p style={{ margin: "0 0 6px" }}>
          The chart is stored in the ESP32's flash and survives power cycles. It is only replaced once the
          ESP32 confirms it saved every row.
        </p>
        <p style={{ margin: "0 0 6px" }}>
          <strong>How the limit is chosen:</strong> at the current boom angle and extension, the safe load is the
          highest limit among rows whose angle is at or below the current angle <em>and</em> whose extension is
          at or beyond the current extension. Between rows it therefore rounds down to the safer row. Outside the
          chart (flatter than the lowest angle, or extended past the longest reach) the limit is 0 kg. A small
          tolerance (0.5° and 5 mm) is applied at the edges so sensor noise doesn't count as outside.
        </p>
        <p style={{ margin: 0 }}>
          With no chart saved, a default limit of 5 kg is used. The chart only drives the alarms (80% warning,
          100% critical); it never blocks motion.
        </p>
      </div>
    </div>
  );
}
