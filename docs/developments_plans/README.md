# Development plans

This folder holds the firmware's architecture decision records (ADRs): what was decided, why, and plans not yet built. It is not the authority for current behaviour; that lives in the source, `sdkconfig.defaults*` and `references/`.

This index says only where each ADR stands. How it got there (device captures, readings, review verdicts) is in the ADR's folder, in its `progress.md` when it has one. Archived ADRs are removed from the tree; `git log --all -- docs/developments_plans/<folder>` brings any of them back.

Status: **Draft**, being discussed, not approved. **Decided**, approved and not built. **Open**, built and flashed, with the part under "Left" still to do. **Built**, built, flashed and verified on the device, nothing left. **Archived**, removed from the tree.

ADRs are written in plain form: owner's direction, context with measured facts, decisions, the plan, verification on the device, and what was built (`progress.md`). The `development-plan` skill was removed on 2026-11-05; ADR-001 to ADR-004 were written to its contract and keep that shape. Only a Decided or Open ADR may be executed.

| ADR | Title | Status | Left | Decision | Where |
|---|---|---|---|---|---|
| 001 | Internal RAM allocation: move non-critical memory to PSRAM | Built | Overnight soak deferred (owner) | Safety net first (core dump, failed-allocation log, stack report), then task stacks to PSRAM, then QSPI back to 4091, each flashed and verified alone; state-change soak (overnight run deferred); later stack-size fix for LVGL and the event loop; no performance change | [ADR-001](ADR-001-internal_ram_allocation/README.md) |
| 002 | Display pipeline performance, one change at a time | Decided (step 1) | Step 1 in progress; steps 2-3 draft | Each performance change is added alone and proven on the device before the next | [ADR-002](ADR-002-display_pipeline_performance/README.md) |
| 003 | Move to the latest ESP-IDF release (v6.1) and drop esp_tinyusb | Draft | Starts after ADR-002 step 1 is Built | Skip v5.5.6; build-first trial, then an executable plan | [ADR-003](ADR-003-esp_idf_6_upgrade/README.md) |
| 004 | Remote control driver and serial interface for automated device tests | Built | Tap effect and switch-off A/B checks deferred (owner) | Two layers: remote_control (LVGL, transport-agnostic) and rc_serial; System settings switch; host runner with verdicts | [ADR-004](ADR-004-remote_control/README.md) |
