# adr-002 step 1: project-owned flush through fixed internal staging buffers

## Summary

- Goal: replace `esp_lvgl_port`'s display path with a project-owned LVGL display whose flush rotates and byte-swaps each strip of the frame straight from the PSRAM draw buffer into one of two fixed internal DMA-capable staging buffers and sends it, so a frame costs one PSRAM read pass, the SPI driver allocates nothing per transaction, and the 255 KB full-frame rotation buffer disappears.
- Plan class: large
- Status: draft (2026-11-05, awaiting owner approval)
- Mandatory skills: `esp32-debug`, `development-plan`
- Overall cutover strategy: bigbang. The port display path is removed in one change; no switch back at runtime. Rollback is `git revert` of the step 1 commit.
- Highest-risk areas: pixel mapping at 90°/180°/270° for partial areas; flush-done signalling (a missed signal hangs LVGL); internal DMA memory layout; tearing.
- Acceptance criteria: picture correct at all four rotations by the owner's eye with tearing no worse than today; flush callback average not worse than today at 180° (30.4 ms) and 90° (43.0 ms); internal largest DMA-capable block ≥ 64 KB; state-change run and an overnight run of at least 8 h with zero failure markers and zero failed-allocation reports.

## Owner's direction (2026-11-05)

1. Performance changes are risky; this step is added alone and proven on the device before step 2.
2. Tearing was diagonal; full-screen redraw of the digital level view and `full_refresh = false` stay as they are.
3. This step changes the memory layout and driver memory allocations, so it is monitored overnight before it is called Built.
4. QSPI transfer size stays at 4091 (4,092-byte descriptors).

## Context

State after ADR-001 (Built 2026-11-05, `main` at `c327b08`), measured on COM11:

| Reading | Rotation 180° | Rotation 90° |
|---|---:|---:|
| Frames/s (`flush=` per 1-s window) | about 16 | 12.9 |
| Flush callback average | 30.4 ms | 43.0 ms |
| Render average (includes flush) | 54.5 ms | 66.3 ms |

Internal free heap about 193 KB, largest DMA-capable block 114.7 KB, PSRAM free about 5.6 MB.

Today's flush (`esp_lvgl_port` 2.7.1, `lvgl_port_flush_callback` in `src/lvgl9/esp_lvgl_port_disp.c`) makes three full passes over the frame before the bus sees it:

1. `lv_draw_sw_rotate` from the LVGL draw buffer into the rotation buffer `draw_buffs[2]` (255,360 B, PSRAM).
2. `lv_draw_sw_rgb565_swap` over that buffer in place (PSRAM).
3. `esp_lcd_panel_draw_bitmap`; the SPI master copies every 4,092-byte chunk from PSRAM into a fresh internal DMA buffer (`spi_master.c`, `setup_priv_desc`), up to 10 in flight.

LVGL 9.6's 90° and 270° rotations (`rotate90_rgb565`, `rotate270_rgb565` in `lvgl__lvgl/src/draw/lv_draw_utils.c`) walk the source column by column, so every source read lands on a different PSRAM cache line; this matches the 43 ms flush at 90°.

ESP-IDF behaviour this design relies on (v5.5.2, `components/esp_lcd/spi/esp_lcd_panel_io_spi.c`): `panel_io_spi_tx_param` and the command phase of `panel_io_spi_tx_color` first wait for every in-flight transaction (lines 215-218, 328-332); `on_color_trans_done` fires once per `tx_color`, on its last chunk (line 377). `esp_lcd_panel_draw_bitmap` on the SH8601 sends the window with `tx_param` and the pixels with `tx_color`. So the next strip's `draw_bitmap` returns only after the previous strip has left the bus, and a buffer whose `draw_bitmap` call has been followed by another `draw_bitmap` call is free.

The SPI master skips its bounce copy when the source is DMA-capable internal memory (`esp_ptr_dma_capable`) and, on the ESP32-S3, needs no address or length alignment for TX.

Features of `lvgl_port_add_disp` this firmware uses, and their replacement:

| Port feature | Replacement in this step |
|---|---|
| Two PSRAM draw buffers, partial render mode | Same, allocated by the new module |
| `rounder_cb` on `LV_EVENT_INVALIDATE_AREA` (SH8601 needs even start / odd end) | Same rounder, registered by the new module |
| LVGL task wake on `LV_EVENT_INVALIDATE_AREA` and `LV_EVENT_REFR_REQUEST` | `lvgl_port_task_wake(LVGL_PORT_EVENT_DISPLAY, NULL)` from the same events |
| `on_color_trans_done` → `lv_display_flush_ready` | New module's callback, after the last strip |
| Software rotation + byte swap | New module's strip copy |
| Hardware rotation on `LV_EVENT_RESOLUTION_CHANGED` when `sw_rotate=false` | Not used (SH8601 has no `swap_xy`/`mirror_y`); not replaced |

`lvgl_port_init`, `lvgl_port_lock`/`unlock`, `lvgl_port_add_touch`, `lvgl_port_stop`/`resume` and `lvgl_port_task_wake` stay in use and are independent of `lvgl_port_add_disp`.

Adjacent improvement areas intentionally left for later steps:

- Rendering into internal buffers, single draw buffer, larger QSPI transfers, 80 MHz QSPI (ADR-002 Deferred).
- Tearing-effect sync on GPIO18 (step 2) and partial redraw of the digital level view (step 3).

## Mandatory Skills

- `esp32-debug` (`.agents/skills/esp32-debug/SKILL.md`): load before building, flashing or capturing serial output.
- `development-plan` (`.agents/skills/development-plan/SKILL.md`): load before executing, verifying or signing off this plan.

## Mandatory Rules

- After any automatic context compaction, the parent or active execution agent must reread this entire plan before continuing implementation, verification, handoff, or final reporting.
- After signing off any major progress checklist stage, the parent or active execution agent must reread this entire plan before starting the next stage.
- Before final completion, lifecycle status changes, merge, or sign-off, the parent agent must run the plan's `Independent Code Review` gate and record the result in `Execution Evidence`.
- The plan's `Execution Model` must use parent-led native subagent execution unless the user explicitly approves fallback execution.
- All ADR-001 build and device rules apply unchanged (`docs/developments_plans/ADR-001-internal_ram_allocation/README.md`, Mandatory Rules): ESP-IDF environment variables, COM11, build only in `build/`, regenerate `sdkconfig` after editing the defaults, never enable `CONFIG_HEAP_TASK_TRACKING` or heap poisoning, the allowed BNO085 first-boot abort, no changes to the device's saved settings, no temporary firmware changes left behind, `HEAPS_CAPS_ALLOC_DEFAULT_FLAGS` for PSRAM allocations.
- Warn the owner before any command that resets the device (`idf.py flash`, `idf.py coredump-info`, any `esptool` call), and never run one while a capture the owner is using is open.
- The flush-done callback runs in the SPI ISR: it must not allocate, block or log.
- Each strip boundary keeps the SH8601 rule: window start row even, end row odd.
- Commit this step as one code commit after Stage 3 passes, plus evidence commits. Push only with the owner's approval.

## Must Do

- Create `main/display_flush.h` / `main/display_flush.c` with the interface in Contracts And Data Shapes.
- In `main/lvgl_display.c`, replace the `lvgl_port_display_cfg_t` block and the `lvgl_port_add_disp` call with `display_flush_create(io_handle, panel_handle)`, called with the LVGL lock held; on `NULL`, `ESP_ERROR_CHECK(ESP_ERR_NO_MEM)`.
- Move `lvgl_port_rounder_divide_by_two` from `main/lvgl_display.c` into `main/display_flush.c` as a `static` function `display_flush_rounder`, unchanged in behaviour; delete the old definition.
- Delete `LVGL_DISPLAY_TRANS_SIZE` from `main/app_cfg.h` (no reader remains) and update the `display_prof_cfg` log line in `main/lvgl_display.c`: replace `lvgl_trans_px=%d` with `staging_bytes=%d` (from `DISPLAY_FLUSH_STAGING_BYTES`) and keep the other fields.
- Run every check in `Verification` and record the results in `progress.md` in this folder.

## Deferred

- Do not change `LCD_QSPI_MAX_TRANSFER_SIZE`, the QSPI clock, the SPI queue depth, the draw-buffer count or size, render mode, `full_refresh`, the digital level view or its invalidation.
- Do not use GPIO18 / tearing-effect sync.
- Do not patch `esp_lvgl_port`, LVGL or `esp_lcd_sh8601`.
- Do not use `lv_draw_sw_rotate` or `lv_draw_sw_rgb565_swap` in the new flush.
- Do not add a runtime switch between the old and new flush.
- Do not change touch, input, low-power or OTA code.

## Cutover Policy

Overall strategy: bigbang

| Area | Policy | Instruction |
|---|---|---|
| LVGL display creation | bigbang | `display_flush_create` replaces `lvgl_port_add_disp`. No fallback. |
| Rotation buffer | bigbang | Not allocated any more. |
| Flush path | bigbang | Project flush only. |
| `LVGL_DISPLAY_TRANS_SIZE` | bigbang | Deleted. |

- The responsible execution agent must follow the selected policy for each area and must not choose a more conservative strategy by default.
- Any change to a cutover policy requires owner approval before implementation.

## Target State

- The LVGL display is created by `display_flush_create`; `esp_lvgl_port` no longer owns a display.
- Two staging buffers of `DISPLAY_FLUSH_STAGING_BYTES` (16,384 B) each live in internal DMA-capable RAM for the life of the firmware; nothing else in the display path allocates internal memory per frame or per transaction.
- Each flush reads the LVGL area once from PSRAM, writes rotated and byte-swapped strips into the staging buffers, and sends each strip with `esp_lcd_panel_draw_bitmap`; LVGL is told the flush is done when the last strip has left the bus.
- The full-frame rotation buffer (255,360 B PSRAM) is gone.
- What the user sees is unchanged apart from speed: same picture at all four rotations, same tearing behaviour.

## Design Decisions

| Topic | Decision | Rationale |
|---|---|---|
| Who creates the LVGL display | The project (`display_flush_create`), not `lvgl_port_add_disp` | Replacing only the flush callback would leave the port allocating an unused 255 KB rotation buffer (`sw_rotate=true`) or calling unsupported hardware rotation on every rotation change (`sw_rotate=false`), plus a second `on_color_trans_done` owner. |
| Staging size | 2 × 16,384 B, `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`, allocated once | 29 full rows (16,240 B) per strip at 280 px; 32 KB fits in the 114.7 KB largest DMA block with ≥ 64 KB left. |
| Rows per strip | `floor(DISPLAY_FLUSH_STAGING_BYTES / (physical_width × 2))`, rounded down to an even number | Keeps every window start even and end odd for the SH8601. |
| Overlap | Two buffers alternate; no semaphore | `draw_bitmap` for strip k+1 returns only after strip k has left the bus (ESP-IDF waits in `tx_param`), so the buffer of strip k is free when strip k+1's call returns. |
| Flush done | Count strips; the ISR callback calls `lv_display_flush_ready` when the done count reaches the frame's strip count | Callbacks of earlier strips can arrive while the next `draw_bitmap` waits; a "last strip" flag set beforehand would fire early. |
| Rotate loops | Project loops that read PSRAM row by row and write strided into internal RAM, with the byte swap in the same pass | One PSRAM read pass and sequential cache-line use at every rotation. |
| Pixel mapping | Identical to `esp_lvgl_port` 2.7.1 with LVGL 9.6 (table below) | The picture must not change. |
| Draw buffers | Unchanged: 2 × full frame, PSRAM, partial mode | One change at a time. |

## Contracts And Data Shapes

### Public interface (`main/display_flush.h`)

```c
#define DISPLAY_FLUSH_STAGING_BYTES 16384

// Create the LVGL display for the SH8601 panel: two full-frame RGB565 draw
// buffers in PSRAM (HEAPS_CAPS_ALLOC_DEFAULT_FLAGS, CONFIG_LV_DRAW_BUF_ALIGN),
// partial render mode, two internal DMA staging buffers, the flush callback,
// the even/odd rounder and LVGL task wake on LV_EVENT_INVALIDATE_AREA and
// LV_EVENT_REFR_REQUEST, and the panel IO on_color_trans_done callback.
// Call with the LVGL lock held, after lvgl_port_init(). Returns NULL and frees
// everything it allocated if any allocation fails.
lv_display_t *display_flush_create(esp_lcd_panel_io_handle_t io_handle,
                                   esp_lcd_panel_handle_t panel_handle);
```

No other symbol is public. Everything else in `display_flush.c` is `static`.

### Area and pixel mapping

Let the LVGL area be `w × h` pixels at logical `(x1, y1)`, source buffer `src` with stride `w` pixels. Physical panel resolution 280 × 456; at 90°/270° LVGL's logical resolution is 456 × 280. The physical window is computed exactly as `lvgl_port_rotate_area` does in `esp_lvgl_port` 2.7.1 (`hres`/`vres` swapped at 90°/270°):

| Rotation | Physical width × height | Physical window | Source pixel `(sx, sy)` goes to window pixel `(dx, dy)` |
|---|---|---|---|
| 0° | `w × h` | unchanged | `dx = sx`, `dy = sy` |
| 90° | `h × w` | `y2 = vres - x1 - 1; x1' = y1; x2 = x1' + h - 1; y1' = y2 - w + 1` | `dx = sy`, `dy = w - 1 - sx` |
| 180° | `w × h` | `y2 = vres - y1 - 1; y1' = y2 - h + 1; x2 = hres - x1 - 1; x1' = x2 - w + 1` | `dx = w - 1 - sx`, `dy = h - 1 - sy` |
| 270° | `h × w` | `x1' = hres - y2 - 1; y2' = x2; x2' = x1' + h - 1; y1' = y2' - w + 1` | `dx = h - 1 - sy`, `dy = sx` |

Every written pixel is byte-swapped (`(p >> 8) | (p << 8)`).

### Strip loop (per flush)

1. Compute the physical window and `rows = rows per strip` for its width.
2. `strips_total = ceil(physical_height / rows)`; reset `strips_done = 0`.
3. For strip `k` with window rows `[r0, r1)`, fill staging buffer `k % 2`:
   - 0°: copy source rows `r0..r1-1` with swap.
   - 180°: for each window row `dy`, read source row `h - 1 - dy` and write it reversed with swap.
   - 90°: for each source row `sy` in `0..h-1`, read the contiguous source columns `sx` from `w - r1` to `w - 1 - r0`, and write each to `dst[(dy - r0) × pw + sy]` with `dy = w - 1 - sx`.
   - 270°: for each source row `sy`, read source columns `r0..r1-1`, and write each to `dst[(sx - r0) × pw + (h - 1 - sy)]`.
4. `esp_lcd_panel_draw_bitmap(panel, x1', y1' + r0, x2' + 1, y1' + r1, staging[k % 2])`.
5. On a `draw_bitmap` error: `ESP_LOGE` once per flush, set `strips_total` to the number of strips queued successfully, and if `strips_done` already equals it, call `lv_display_flush_ready` from the task; otherwise the ISR does. The frame is dropped; LVGL must never be left waiting.

### Flush-done callback (ISR)

`IRAM_ATTR`; increments `strips_done`; when it equals `strips_total`, calls `lv_display_flush_ready(disp)`; returns `false`. No allocation, blocking or logging. The compare of `strips_done` and `strips_total` between task and ISR uses a `portMUX_TYPE` critical section.

## Change Surface

### Create

- `main/display_flush.h`, `main/display_flush.c`: the module above. Public entrypoint `display_flush_create()`.
- `docs/developments_plans/ADR-002-display_pipeline_performance/progress.md`: execution evidence.

### Modify

- `main/lvgl_display.c`: display creation call; delete `lvgl_port_rounder_divide_by_two`; `display_prof_cfg` log line.
- `main/app_cfg.h`: delete `LVGL_DISPLAY_TRANS_SIZE`.
- `docs/developments_plans/README.md` and `docs/developments_plans/ADR-002-display_pipeline_performance/README.md`: status at sign-off.

### Keep

- `main/bsp.c` (panel and IO setup, queue depth 10, QSPI 4091), touch setup, the display profiler, everything outside the display path.

## Overdesign Guardrail

- Actual problem: each frame makes three full PSRAM passes and the SPI driver allocates internal DMA memory per chunk; at 90° the rotation reads PSRAM column by column.
- Minimal change: one module that owns display creation and a strip flush through two fixed staging buffers.
- Ownership boundaries: `display_flush` owns the LVGL display and the panel IO completion callback; `lvgl_display.c` owns startup order, touch, buttons and the profiler; `esp_lvgl_port` keeps the LVGL task, lock, touch and stop/resume.
- Rejected complexity: a runtime switch, a third staging buffer, DMA-capable PSRAM draw buffers, an RTOS queue between rotate and send, tearing sync, partial-render changes, patching the port.
- Evidence threshold: a measured case where the bus waits on the copy (flush time above bus time plus copy time of one strip), or tearing visible after this step, is needed before adding any of the rejected items.

## Agent Autonomy Boundaries

- The responsible agent may choose local implementation mechanics only when they preserve the contracts in this plan.
- The responsible agent must not introduce new architecture, alternate migration strategies, compatibility layers, fallback paths, or extra features.
- Changes outside `main/display_flush.*`, `main/lvgl_display.c` and `main/app_cfg.h` are not allowed.
- The responsible agent must not perform unrelated cleanup, formatting churn, dependency upgrades, or broad refactors.
- If the plan and code disagree, the responsible agent must preserve the plan's stated intent and report the discrepancy.
- If a required instruction is impossible, the responsible agent must stop and report the blocker instead of inventing a substitute.

## Implementation Order

1. Parent records the starting point: Static Greps on the unchanged tree; heap, flush time and frames at 180° and 90° from the ADR-001 evidence (no new capture).
2. Production subagent creates `display_flush.*` and changes `lvgl_display.c` and `app_cfg.h`.
3. Parent runs Static Greps, builds with `DISPLAY_PROFILING_ENABLED 1` and the trace facility on, flashes, runs the Stage 3 device checks with the owner.
4. Parent commits the code change, then runs the state-change run and the overnight run.
5. Parent builds the final configuration (profiling off, `sdkconfig` regenerated), flashes, checks one clean boot.
6. Independent code review, fixes, then status updates.

## Execution Model

- Parent agent owns orchestration, static checks, builds, flashing, device captures, execution evidence, review feedback remediation, commits, lifecycle updates, and final sign-off.
- Parent agent establishes the focused contract first (Static Greps on the unchanged tree) before production implementation starts.
- Production-code subagent: exactly one native subagent, started after the focused contract is established; owns the production code changes in Change Surface only; closes after they are complete, excluding review fixes.
- Independent code-review subagent: exactly one native subagent, started after planned verification passes; reviews the plan, diff, and evidence; reports findings to the parent; does not implement fixes.
- If native subagent capability is unavailable, stop before execution unless the user explicitly requests fallback execution.

## Progress Checklist

- [ ] Stage 1 - starting point recorded
  - Covers: step 1.
  - Verify: Static Greps on the unchanged tree (expected: 1 `lvgl_port_add_disp(`, 1 `LVGL_DISPLAY_TRANS_SIZE` define, 0 `display_flush_create(`).
  - Evidence: counts and the starting-point table in `progress.md`.
  - Sign-off: `<agent/date>`
- [ ] Stage 2 - production change complete
  - Covers: step 2.
  - Verify: Static Greps after the change; `idf.py build` passes with no warnings from changed files.
  - Evidence: grep output, build tail, `idf.py size` DIRAM line.
  - Sign-off: `<agent/date>`
- [ ] Stage 3 - device checks
  - Covers: step 3.
  - Verify: Device checks "Boot and memory", "Picture", "Speed".
  - Evidence: captures, readings, the owner's verdict at each rotation, commit hash of the code change.
  - Sign-off: `<agent/date>`
- [ ] Stage 4 - soak
  - Covers: step 4.
  - Verify: Device checks "State-change run" and "Overnight run".
  - Evidence: capture paths, owner actions, markers, stack reports, heap at start and end.
  - Sign-off: `<agent/date>`
- [ ] Stage 5 - final build flashed
  - Covers: step 5.
  - Verify: profiling and trace facility off; build; one clean boot; `git status` shows only the change surface.
  - Evidence: boot excerpt, `git status --short`.
  - Sign-off: `<agent/date>`
- [ ] Stage 6 - independent code review
  - Covers: step 6, review part.
  - Verify: review subagent reports; in-surface fixes; greps and build rerun.
  - Evidence: findings, fixes, reruns, approval status.
  - Sign-off: `<agent/date>`
- [ ] Stage 7 - lifecycle
  - Covers: step 6, status part.
  - Verify: ADR-002 step 1 marked Built in its README and the registry.
  - Sign-off: `<agent/date>`

## Verification

### Static Greps

- `rg -n "lvgl_port_add_disp\(|lvgl_port_display_cfg_t" main` returns no matches.
- `rg -n "LVGL_DISPLAY_TRANS_SIZE|trans_size" main` returns no matches.
- `rg -n "display_flush_create\(" main` returns exactly three lines: the declaration in `display_flush.h`, the definition in `display_flush.c`, the call in `lvgl_display.c`.
- `rg -n "lv_draw_sw_rotate|lv_draw_sw_rgb565_swap" main` returns no matches.
- `rg -n "MALLOC_CAP_DMA" main/display_flush.c` returns the staging allocation.

### Build

- `idf.py build` passes in `build/`.

### Device

Verification build: `DISPLAY_PROFILING_ENABLED 1`, `CONFIG_FREERTOS_USE_TRACE_FACILITY=y` in the local `sdkconfig`. Failure markers as in ADR-001 (zero allowed except the BNO085 first-boot abort), plus any LVGL hang (no `display_prof` line for more than 5 s while the screen is active).

- Boot and memory (Wi-Fi off, 330 s): average `largest_dma` ≥ 64,000 B; `free_spiram` higher than the ADR-001 reading (5,615,000 B) by at least 200,000 B; no `alloc_failed:`; stack report: `taskLVGL` ≥ 2,048 B free.
- Picture (owner on the device): at each rotation 0°, 90°, 180°, 270° (set in Settings, Save), the picture is the right way round, colours are correct, the digital level and the status bar update without artefacts, swiping between tiles shows no misplaced strips, and tearing is no worse than before.
- Speed: 120 s at 180° and 120 s at 90° on the digital level view. Flush callback average at 180° ≤ 30.4 ms and at 90° ≤ 43.0 ms; record frames, render and flush times.
- State-change run (owner, as in ADR-001 Stage 5): Wi-Fi on and Save, every tile, three idle/wake cycles, rotation change and back with Save, Wi-Fi off and on. Zero failure markers; `taskLVGL` ≥ 2,048 B and `sys_evt` ≥ 512 B free; application tasks ≥ 512 B.
- Overnight run (≥ 8 h, Wi-Fi on, unattended, logged in the background): zero failure markers and zero `alloc_failed:`; boot count stays at one; heap and stack readings recorded at the start and the end.

## Independent Code Review

Run this gate after all `Verification` checks pass and before final sign-off. The parent agent creates one independent code-review subagent through the harness's native subagent capability. If native subagents are unavailable, stop unless the user explicitly approves fallback execution.

Review scope:

- The pixel mapping and window arithmetic against the Contracts table and `esp_lvgl_port` 2.7.1, for full and partial areas at all four rotations, including odd area sizes after the rounder.
- The flush-done logic: no early, missed or double `lv_display_flush_ready`, including the error path.
- ISR rules in the completion callback; staging buffer reuse safety against ESP-IDF's in-flight waits.
- Allocation and cleanup in `display_flush_create`, including the failure paths.
- Alignment with Change Surface, Deferred, Cutover Policy and Agent Autonomy Boundaries.
- Evidence quality in `progress.md`.

The parent fixes concrete findings only inside the change surface, reruns Static Greps and the build, and records findings, fixes and approval status in `progress.md`.

## Acceptance Criteria

This step is complete when:

- The Static Greps, the build and every device check pass as written above.
- The owner confirms the picture at all four rotations.
- The final build has profiling off and boots cleanly.
- The independent code review has approved, and `progress.md` holds the evidence.
- ADR-002 step 1 says Built in its README and the registry.

## Risks

| Risk | Mitigation | Verification |
|---|---|---|
| Wrong pixels at one rotation or for partial areas | Mapping fixed in the Contracts table, identical to the port | Owner's picture check at all four rotations; review gate |
| LVGL hangs waiting for flush done | Strip counting; error path always completes the frame | Hang marker in every capture; state-change and overnight runs |
| Staging buffer overwritten while on the bus | Reuse only after the next `draw_bitmap` returns (ESP-IDF waits for in-flight) | Picture check; review gate |
| Internal DMA memory shortage under Wi-Fi | 32 KB fixed; per-chunk bounce buffers removed | `largest_dma` ≥ 64,000 B; zero `alloc_failed:` overnight |
| Tearing changes | Same top-to-bottom window order as today | Owner's tearing check |
