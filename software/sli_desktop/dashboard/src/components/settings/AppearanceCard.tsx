import { THEMES, setTheme, useTheme, type ThemeId } from "../../theme";

/** Fixed preview colours per theme (the card shows all three at once, whatever is active). */
const PREVIEW: Record<ThemeId, { bg: string; surface: string; text: string; line: string; accent: string; warn: string; crit: string }> = {
  night:     { bg: "#000000", surface: "#15171A", text: "#F5F6F8", line: "#3B4048", accent: "#3DB8F5", warn: "#FFB224", crit: "#FF4D4D" },
  control:   { bg: "#D5D8DC", surface: "#F8F9FA", text: "#0E1114", line: "#8E949B", accent: "#0A5AA8", warn: "#FFB020", crit: "#C01818" },
  projector: { bg: "#FFFFFF", surface: "#F3F4F6", text: "#000000", line: "#2A2E33", accent: "#0047AB", warn: "#FFAA00", crit: "#B00000" },
};

/** Theme picker: Night (default), Control room (ISA-101 light grey), Projector. Saved per PC. */
export function AppearanceCard() {
  const theme = useTheme();
  return (
    <div className="settings-card">
      <div className="section-label">Appearance</div>
      <div className="theme-grid" role="radiogroup" aria-label="Theme">
        {THEMES.map(t => {
          const p = PREVIEW[t.id];
          return (
            <button key={t.id} role="radio" aria-checked={theme === t.id}
                    className={`theme-opt ${theme === t.id ? "on" : ""}`} onClick={() => setTheme(t.id)}>
              <span className="theme-swatch" style={{ background: p.bg, borderColor: p.line }}>
                <span className="theme-swatch-card" style={{ background: p.surface, borderColor: p.line }}>
                  <span style={{ color: p.text }}>0.62 kg</span>
                  <span className="theme-swatch-dots">
                    <i style={{ background: p.accent }} /><i style={{ background: p.warn }} /><i style={{ background: p.crit }} />
                  </span>
                </span>
              </span>
              <span className="theme-name">{t.name}</span>
              <span className="theme-hint">{t.hint}</span>
            </button>
          );
        })}
      </div>
    </div>
  );
}
