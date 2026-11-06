# Development plans

This folder holds the firmware's architecture decision records (ADRs): what was decided, why, and plans not yet built. It is not the authority for current behaviour; that lives in the source, `sdkconfig.defaults*` and `references/`.

This index says only where each ADR stands. How it got there (device captures, readings, review verdicts) is in the ADR's folder, in its `progress.md` when it has one. Archived ADRs are removed from the tree; `git log --all -- docs/developments_plans/<folder>` brings any of them back.

Status: **Draft**, being discussed, not approved. **Decided**, approved and not built. **Open**, partly built, with the part under "Left" still to do. **Built**, built, flashed and verified on the device, nothing left. **Archived**, removed from the tree.

How an ADR is written: in plain words and good intent. What we want, the owner's direction, what we know (measured facts, checked in the code), what we will do and why, and how the device shows it works; `progress.md` records what was built and measured. No mandatory sections, stage checklists, sign-offs, execution models or agent rules. Work on an ADR starts when the owner approves it, and one ADR is open at a time: every step of one is Built before the next starts. Functional changes and dependency updates are separate ADRs that never cross over (ADR-002 functional on today's dependencies; ADR-003 the dependency update with no functional change).

ADR-001 and ADR-004 are in the retired `development-plan` format. They are legacy: never follow them, cite them as rules, or copy their layout; they stay only as a record of what was done.

| ADR | Title | Status | Left | Decision | Where |
|---|---|---|---|---|---|
| 001 | Internal RAM allocation: move non-critical memory to PSRAM | Built (legacy format) | — (overnight soak waived by the owner 2026-11-06) | Safety net first (core dump, failed-allocation log, stack report), then task stacks to PSRAM, then QSPI back to 4091, each flashed and verified alone; state-change soak (overnight run deferred); later stack-size fix for LVGL and the event loop; no performance change | [ADR-001](ADR-001-internal_ram_allocation/README.md) |
| 002 | Display pipeline performance, one change at a time | Open | Steps 1 and 2 (TE sync) Built; step 3 (partial redraw) approved 2026-11-06, in progress | Each performance change is added alone and proven on the device before the next | [ADR-002](ADR-002-display_pipeline_performance/README.md) |
| 003 | Move to the latest ESP-IDF release (v6.1) and drop esp_tinyusb | Draft | Starts only after ADR-002 is Built (all steps; owner 2026-11-06) | Skip v5.5.6; build-only trial, then the list of changes for the owner to approve | [ADR-003](ADR-003-esp_idf_6_upgrade/README.md) |
| 004 | Remote control driver and serial interface for automated device tests | Built (legacy format) | Tap effect and switch-off checks deferred (owner) | Two layers: remote_control (LVGL, transport-agnostic) and rc_serial; System settings switch; host runner with verdicts | [ADR-004](ADR-004-remote_control/README.md) |
