# Research Paper / Publication

Status: not started (see the README's *Research Paper / Publication* section — planned for October 2026,
per the project timeline).

When drafting begins, this file should hold the paper's outline, draft sections, and target
conference or journal, distinct from [docs/literature_survey.md](../docs/literature_survey.md) (the
background reading) and [docs/project_status_report.md](../docs/project_status_report.md) (the
project's current status).

## Suggested outline

1. Introduction and problem statement (see the README's Problem Statement and Abstract)
2. Related work (from `docs/literature_survey.md`)
3. System design (three-layer architecture — see `software/software.md`)
4. Load-angle compensation method (learned per-angle correction, not a fixed formula — see
   `software/software.md`, "Safety behaviour")
5. Safety logic: load chart, alarm levels, sensor-fault detection, IMU-versus-encoder cross-check
6. Results (once hardware testing is complete)
7. Conclusion and future work (PID anti-sway, tipping prediction, real-crane integration)
