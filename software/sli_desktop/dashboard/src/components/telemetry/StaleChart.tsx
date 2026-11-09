import { useEffect, useState } from "react";
import type { LoadChartProfile } from "../../types";
import { API_BASE } from "../../api";

const same = (a: number[], b: number[]) => a.length === b.length && a.every((v, i) => Math.abs(v - b[i]) < 1e-6);

/**
 * The crane enforces the chart stored in the ESP32's flash, not the profile on
 * this PC. When the PC's profile of the same name has different axes (e.g. the
 * extensions were rebased to retracted = 0 mm), say so and offer the upload:
 * otherwise the dashboard and the crane disagree about where the chart is.
 */
export function StaleChart({ crane }: { crane: LoadChartProfile | null | undefined }) {
  const [saved, setSaved] = useState<LoadChartProfile | null>(null);
  const [msg, setMsg] = useState("");
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    if (!crane) return;
    fetch(`${API_BASE}/loadchart/profiles`).then(r => r.json())
      .then((ps: LoadChartProfile[]) => setSaved(ps.find(p => p.name === crane.name) ?? null))
      .catch(() => setSaved(null));
  }, [crane]);

  if (!crane || !saved || (same(crane.extensionsMM, saved.extensionsMM) && same(crane.anglesDeg, saved.anglesDeg))) return null;

  const upload = async () => {
    setBusy(true);
    setMsg("Uploading…");
    try {
      const r = await fetch(`${API_BASE}/loadchart/profiles/${encodeURIComponent(saved.name)}/upload`, { method: "POST" });
      const d = await r.json().catch(() => ({}));
      setMsg(r.ok ? "Sent. The crane confirms in a moment." : (d.detail ?? "Upload failed"));
      if (r.ok) setTimeout(() => fetch(`${API_BASE}/loadchart/readback`, { method: "POST" }).catch(() => null), 1500);
    } catch {
      setMsg("Backend offline");
    }
    setBusy(false);
  };

  return (
    <div className="stale-chart">
      <div>
        The crane runs an <b>old copy of “{crane.name}”</b>: extensions {crane.extensionsMM.join(" / ")} mm.
        The saved profile has {saved.extensionsMM.join(" / ")} mm.
      </div>
      <div className="stale-chart-row">
        <button className="btn-primary btn-sm" onClick={upload} disabled={busy}>Upload saved “{saved.name}” to the crane</button>
        {msg && <span className="dl-muted">{msg}</span>}
      </div>
    </div>
  );
}
