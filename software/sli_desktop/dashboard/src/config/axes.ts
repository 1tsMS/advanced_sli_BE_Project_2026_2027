export type AxisId = 1 | 2 | 3 | 4;

export interface AxisDef {
  id: AxisId;
  name: string;
  /** Label of the "negative" and "positive" buttons on screen */
  negLabel: string;
  posLabel: string;
  /**
   * G-code direction (D0 / D1) sent for each side. The on-screen buttons AND
   * the gamepad both read these, so if an axis turns out to move the wrong
   * way on the real crane, swap negDir/posDir here — one place fixes both.
   * (The design plan lists M2/M4 D0 as "up"; the dashboard has always used
   * D1 for ▲. Verify on hardware.)
   */
  negDir: 0 | 1;
  posDir: 0 | 1;
}

export const AXES: AxisDef[] = [
  { id: 1, name: "Swing",  negLabel: "◄", posLabel: "►", negDir: 0, posDir: 1 },
  { id: 2, name: "Boom",   negLabel: "▼", posLabel: "▲", negDir: 0, posDir: 1 },
  { id: 3, name: "Extend", negLabel: "◄", posLabel: "►", negDir: 0, posDir: 1 },
  { id: 4, name: "Winch",  negLabel: "▼", posLabel: "▲", negDir: 0, posDir: 1 },
];
