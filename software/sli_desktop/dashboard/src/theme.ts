import { useSyncExternalStore } from "react";

/**
 * Colour theme (tokens in index.css). Stored per PC in localStorage and put on
 * <html data-theme>, so the whole stylesheet switches with one attribute.
 */
export type ThemeId = "night" | "control" | "projector";

export const THEMES: { id: ThemeId; name: string; hint: string }[] = [
  { id: "night",     name: "Night",        hint: "Black, high contrast. Default for the lab bench." },
  { id: "control",   name: "Control room", hint: "ISA-101 light grey: calm when normal, colour only for alarms." },
  { id: "projector", name: "Projector",    hint: "White, larger type, heavier lines: review projector and report screenshots." },
];

const KEY = "sli-theme";
const listeners = new Set<() => void>();

function stored(): ThemeId {
  try {
    const v = localStorage.getItem(KEY);
    if (v === "night" || v === "control" || v === "projector") return v;
  } catch { /* storage blocked: default */ }
  return "night";
}

let current: ThemeId = stored();

/** Call once before the first render (main.tsx) so there is no flash of the wrong theme. */
export function applyStoredTheme() {
  document.documentElement.dataset.theme = current;
}

export function setTheme(t: ThemeId) {
  current = t;
  document.documentElement.dataset.theme = t;
  try { localStorage.setItem(KEY, t); } catch { /* not saved, still applied */ }
  listeners.forEach(l => l());
}

export function useTheme(): ThemeId {
  return useSyncExternalStore(
    cb => { listeners.add(cb); return () => listeners.delete(cb); },
    () => current,
  );
}
