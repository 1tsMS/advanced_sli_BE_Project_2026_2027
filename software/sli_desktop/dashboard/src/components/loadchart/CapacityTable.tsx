import { Plus, Minus } from "lucide-react";
import type { LoadChartProfile } from "../../types";
import { LC_MAX_ANGLES, LC_MAX_EXTS, rampColor } from "./chartMath";

interface CapacityTableProps {
  draft: LoadChartProfile;
  onChange: (p: LoadChartProfile) => void;
  /** Chart cell the live pose sits in ([row, col] of its lower corner), or null. */
  liveCell: [number, number] | null;
  /** Chart on the crane, to flag cells that differ (null = not read / none). */
  crane: LoadChartProfile | null;
}

const num = (s: string) => (s.trim() === "" ? NaN : Number(s));
const show = (v: number) => (Number.isFinite(v) ? String(v) : "");

/**
 * Rated-capacity table like a printed crane chart: rows are boom angles,
 * columns are telescope extensions, cells are kg. Every axis value and cell is
 * editable; the colour strip on each cell uses the same ramp as the diagram.
 */
export function CapacityTable({ draft, onChange, liveCell, crane }: CapacityTableProps) {
  const A = draft.anglesDeg, E = draft.extensionsMM, K = draft.capacityKg;
  const finite = K.flat().filter(Number.isFinite);
  const lo = Math.min(...finite), hi = Math.max(...finite);
  const sameShape = !!crane && crane.anglesDeg.length === A.length && crane.extensionsMM.length === E.length;

  const set = (patch: Partial<LoadChartProfile>) => onChange({ ...draft, ...patch });
  const setCell = (i: number, j: number, v: number) =>
    set({ capacityKg: K.map((r, ri) => (ri === i ? r.map((c, cj) => (cj === j ? v : c)) : r)) });
  const setAngle = (i: number, v: number) => set({ anglesDeg: A.map((a, k) => (k === i ? v : a)) });
  const setExt = (j: number, v: number) => set({ extensionsMM: E.map((e, k) => (k === j ? v : e)) });

  // New rows / columns copy the last one: never invent a bigger capacity
  const addRow = () => set({
    anglesDeg: [...A, (A[A.length - 1] ?? 0) + 10],
    capacityKg: [...K, [...K[K.length - 1]]],
  });
  const delRow = () => set({ anglesDeg: A.slice(0, -1), capacityKg: K.slice(0, -1) });
  const addCol = () => set({
    extensionsMM: [...E, (E[E.length - 1] ?? 0) + 10],
    capacityKg: K.map(r => [...r, r[r.length - 1]]),
  });
  const delCol = () => set({ extensionsMM: E.slice(0, -1), capacityKg: K.map(r => r.slice(0, -1)) });

  return (
    <div className="lc-table-wrap">
      <table className="lc-table">
        <thead>
          <tr>
            <th className="lc-corner">
              <span>Boom °</span><span>Ext mm →</span>
            </th>
            {E.map((e, j) => (
              <th key={j}>
                <input className="lc-axis-in" type="number" step="any" value={show(e)}
                       aria-label={`Extension ${j + 1} (mm)`}
                       onChange={ev => setExt(j, num(ev.target.value))} />
              </th>
            ))}
            <th className="lc-addcol">
              <button className="lc-mini" onClick={addCol} disabled={E.length >= LC_MAX_EXTS} title="Add extension column">
                <Plus size={10} />
              </button>
              <button className="lc-mini" onClick={delCol} disabled={E.length <= 2} title="Remove last column">
                <Minus size={10} />
              </button>
            </th>
          </tr>
        </thead>
        <tbody>
          {A.map((a, i) => (
            <tr key={i}>
              <th>
                <input className="lc-axis-in" type="number" step="any" value={show(a)}
                       aria-label={`Boom angle ${i + 1} (deg)`}
                       onChange={ev => setAngle(i, num(ev.target.value))} />
              </th>
              {E.map((_, j) => {
                const v = K[i][j];
                const bad = !(Number.isFinite(v) && v >= 0.001 && v <= 10);
                const craneV = sameShape ? crane!.capacityKg[i]?.[j] : undefined;
                const differs = craneV !== undefined && Math.abs(craneV - v) > 0.0015;
                const isLive = !!liveCell && (liveCell[0] === i || liveCell[0] + 1 === i) &&
                               (liveCell[1] === j || liveCell[1] + 1 === j);
                return (
                  <td key={j} className={`lc-cell${bad ? " bad" : ""}${differs ? " differs" : ""}${isLive ? " live" : ""}`}
                      title={differs ? `On the crane: ${craneV} kg` : undefined}>
                    <span className="lc-cell-strip" style={{ background: bad ? "transparent" : rampColor(v, lo, hi) }} />
                    <input type="number" step="0.01" min={0.001} max={10} value={show(v)}
                           aria-label={`Capacity at ${a}° and ${E[j]} mm (kg)`}
                           onChange={ev => setCell(i, j, num(ev.target.value))} />
                  </td>
                );
              })}
              <td />
            </tr>
          ))}
          <tr>
            <th className="lc-addrow">
              <button className="lc-mini" onClick={addRow} disabled={A.length >= LC_MAX_ANGLES} title="Add boom angle row">
                <Plus size={10} />
              </button>
              <button className="lc-mini" onClick={delRow} disabled={A.length <= 2} title="Remove last row">
                <Minus size={10} />
              </button>
            </th>
          </tr>
        </tbody>
      </table>
      <div className="lc-table-note">kg rated payload · cells between rows/columns are interpolated · outlined = live pose</div>
    </div>
  );
}
