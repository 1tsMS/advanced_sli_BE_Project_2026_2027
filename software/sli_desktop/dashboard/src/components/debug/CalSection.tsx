import type { ReactNode } from "react";
import { ChevronDown } from "lucide-react";

export type CalStatus = { text: string; level: "ok" | "warn" | "off" };

/**
 * One calibration step as a collapsible section. The header always shows the
 * step number, what it calibrates and whether the ESP32 has a stored value, so
 * the closed list reads as a checklist; the body holds the controls.
 */
export function CalSection({ n, title, summary, status, open, onToggle, children }: {
  n: number; title: string; summary: string; status: CalStatus;
  open: boolean; onToggle: () => void; children: ReactNode;
}) {
  return (
    <section className={`cal-sec ${open ? "open" : ""}`}>
      <button className="cal-sec-head" onClick={onToggle} aria-expanded={open}>
        <span className="cal-sec-n">{n}</span>
        <span className="cal-sec-text">
          <span className="cal-sec-title">{title}</span>
          <span className="cal-sec-sum">{summary}</span>
        </span>
        <span className={`state-chip ${status.level}`}>{status.level === "warn" ? "△ " : ""}{status.text}</span>
        <ChevronDown size={18} className="cal-sec-chev" />
      </button>
      {open && <div className="cal-sec-body">{children}</div>}
    </section>
  );
}

/** Numbered how-to steps at the top of a calibration body. */
export function CalSteps({ steps, action }: { steps: string[]; action?: ReactNode }) {
  return (
    <div className="cal-intro">
      <ol className="cal-steps">{steps.map((s, i) => <li key={i}>{s}</li>)}</ol>
      {action && <div className="cal-intro-action">{action}</div>}
    </div>
  );
}
