import type { DebugReport } from "../../types";

/** $D,LOAD_CELL verdicts from the ESP32 (firmware loadcell_health.h). */
export const LOAD_CELL_FAULTS: Record<string, { title: string; check: string }> = {
  SATURATED: {
    title: "Load cell wire open: reading stuck at the HX711's limit",
    check: "A signal wire (usually green / white, S+ / S-) is broken or loose. Check the 4 load-cell wires at the HX711 and the boom-tip connector.",
  },
  FROZEN: {
    title: "Load cell dead: the reading doesn't move at all",
    check: "A live cell always shows a little noise. Check that the cell is plugged in at the HX711 terminals and no wire is cut.",
  },
  NEGATIVE: {
    title: "Load reads far below zero",
    check: "The excitation wires (usually red / black, E+ / E-) are open, or the cell came loose from its mount. Fix it, then Tare with an empty hook.",
  },
  OPEN: {
    title: "Load cell unplugged: reading sits at 0 counts",
    check: "The HX711 is fine but sees no bridge: all four wires out, the connector off, or a broken cable. Plug the cell back in (E+ red, E- black, S+ green, S- white on most cells).",
  },
  NO_DATA: {
    title: "HX711 stopped sending readings",
    check: "Check the HX711's DT / SCK wires (GPIO 16 / 17) and its power.",
  },
  NO_HX711: {
    title: "HX711 not found",
    check: "Check the HX711 board's power and DT / SCK wires (GPIO 16 / 17), then Scan.",
  },
};

/** The load-cell verdict from a DBG report: null = no report yet, "OK" = healthy. */
export function loadCellVerdict(report: DebugReport | null): string | null {
  return report?.entries.find(e => e.bus === "LOAD_CELL")?.status ?? null;
}
