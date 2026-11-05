# ADR-002 progress

Execution record for [ADR-002](README.md). Times are NZDT.

## Step 1, Stage 1 - starting point (2026-11-05)

Branch `display_pipeline` from `main` at `c327b08`; plan approved by the owner 2026-11-05.

Static Greps on the unchanged tree: `lvgl_port_add_disp(` 1 (`main/lvgl_display.c`); `#define LVGL_DISPLAY_TRANS_SIZE` 1 (`main/app_cfg.h`); `display_flush_create(` 0. As expected.

Starting point (from ADR-001 evidence, no new capture):

| Reading | 180° | 90° |
|---|---:|---:|
| Frames/s | about 16 (15.8-16.8) | 12.9 |
| Flush callback average | 30.4 ms | 43.0 ms |
| Render average | 54.5 ms | 66.3 ms |

Memory: `free_int` about 193 KB; `largest_dma` 114,483-114,688 B; `free_spiram` about 5,615,000 B; `taskLVGL` 8,328 B free of 16,384 under the soak.

Result: pass.

Correction (2026-11-05): the installed port is `esp_lvgl_port` 2.7.2 (re-resolved by the component manager on 2026-11-05 within the manifest's `~2.7.1`); the plan's references to 2.7.1 are corrected to 2.7.2. The code studied for the plan was the installed 2.7.2.

## Step 1, Stage 2 - production change complete (2026-11-05)

- Production subagent created `main/display_flush.h` / `.c` and changed `main/lvgl_display.c` and `main/app_cfg.h`.
- Static Greps: `lvgl_port_add_disp(|lvgl_port_display_cfg_t` 0; `LVGL_DISPLAY_TRANS_SIZE|trans_size` 0; `display_flush_create(` 3 (`display_flush.h:20`, `display_flush.c:273`, `lvgl_display.c:376`); `lv_draw_sw_rotate|lv_draw_sw_rgb565_swap` 0; `MALLOC_CAP_DMA` in `display_flush.c` 1 (line 295). Pass.
- Mapping check by the production subagent (host simulation of the port path with LVGL 9.6's rotate functions and `lvgl_port_rotate_area`, against the new strip loops; script kept outside the repo): 244 cases (full frame and 60 random rounded partial areas per rotation), physical framebuffer identical in every case, every strip window starts even and ends odd, every strip fits the staging buffer.
- Plan correction: rows per strip at 280 px are 28 (15,680 B; 17 strips per full frame), not 29: the plan's own even-row rule. Fixed in the plan.
- Discrepancies reported by the subagent and accepted as written: source stride from `lv_draw_buf_width_to_stride` (equal to `w` with `CONFIG_LV_DRAW_BUF_STRIDE_ALIGN=1`); `LV_EVENT_FLUSH_FINISH` now receives LVGL's logical area, not the port's rewritten physical one (the profiler uses only its timing); rotation read with `lv_display_get_rotation` at flush time; one creation log line, a `_Static_assert` on the staging size and error logs added; failure to add callbacks counts as creation failure. Edge case recorded: if a strip fails partway through `tx_color`, chunks already queued may still be on the bus when the next flush refills that buffer; only the dropped frame's area can show wrong pixels.
- Build: pass (`idf.py reconfigure` needed for the new source file). DIRAM 137,195 B. The only warning involving the new files is LVGL 9.6's `LV_ASSERT_HANDLER_INCLUDE is deprecated`, emitted by `lvgl.h` for every file that includes it (left for ADR-003).

## Step 1, Stage 3 - device checks (in progress)

### Boot and memory (2026-11-05, Wi-Fi off, 330 s, rotation 180°)

- `Display created: draw_buf=2x255360 B (PSRAM) staging=2x16384 B (internal DMA)`; `display_prof_cfg ... staging_bytes=16384 ... qspi_actual_max_transfer=4092 ... rotation=2`.
- Heap (avg after 10 s): `free_int` 198,028; `largest_int` 118,784; `min_int` 194,111; `free_dma` 190,288; `largest_dma` 118,784 (≥ 64,000: pass); `free_spiram` 5,875,115 (+260,115 B against 5,615,000: pass).
- Failure markers: 1 `rst:` (port open), 1 allowed BNO085 abort; all others 0, including `alloc_failed:`. No `display_prof` gap while the screen was active; the one gap (73.7 s → 311.8 s) matches `Entering idle mode` / `Exiting idle mode` exactly.
- Speed at 180° from this capture (windows with ≥ 10 flushes, 81 windows): frames 17.3/s, render 42.4 ms, flush callback 19.2 ms (was 30.4 ms).
- Stack report: `sys_evt` 1,804 B free; the `taskLVGL` line was lost by the USB console after the idle period; taken from the next capture.

### Picture and speed (2026-11-05, owner on the device, Wi-Fi off)

Capture from uptime 330 s (same boot as the boot/memory check, no reset). Rotation changes in the log: 2 → 0 (532 s) → 1 (550 s) → 2 (702 s). No change to rotation 3 (270°) appears in this boot. The owner used the countdown timer to keep the screen awake and redrawing during the speed windows.

- Owner verdict: "All good now, no visible change to the ADR-001" (picture correct, no artefacts, tearing unchanged).
- Speed (windows with ≥ 10 flushes):

| Rotation | Windows | Frames/s | Render | Flush callback | ADR-001 flush |
|---|---:|---:|---:|---:|---:|
| 0° | 13 | 17.0 | 45,195 µs | 21,282 µs | not measured |
| 90° | 151 | 20.2 | 38,157 µs | 16,226 µs | 43.0 ms |
| 180° | 270 | 19.0 | 41,618 µs | 17,812 µs | 30.4 ms |

  Flush at 180° ≤ 30.4 ms: pass. Flush at 90° ≤ 43.0 ms: pass.
- Heap: `free_int` 198,068; `largest_dma` 118,784; `free_spiram` 5,874,798. Failure markers: all 0. Stack report: `taskLVGL` 8,136 B free; `sys_evt` 1,788 B free.
- Open: 270° not exercised on the device in this capture; the host simulation covered it (identical output).

### 270° check (2026-11-05)

- The owner set 270° and then 0° and reports both correct ("It is done already, 270 deg and 0 deg").
- The 270° selection happened between two captures (uptime 971 s to 1,119 s) and is not in a log. The capture that followed starts with the device at a rotation other than the last logged 180°: flush callback 15.7 ms, 21.7 frames/s over 121 windows (logged 180° earlier: 17.8 ms, 19.0 frames/s), most likely 270°; then `Rotation updated to 1` and `to 0` two seconds apart; 0°: 181 windows, 19.9 frames/s, flush 18.3 ms.
- Failure markers: all 0.

Result for Stage 3: pass. The picture at all four rotations is confirmed by the owner; the 270° speed reading is attributed by elimination, not by a log line.

## Step 1, Stage 4 - soak (in progress)

- Overnight run started 2026-11-05 about 23:45 NZDT on the step 1 verification build (`320bb15` code, profiling and trace facility on), Wi-Fi on, unattended, 9 h capture.
- State-change run: not run by hand. Proposed to run it unattended with the ADR-004 remote-control runner (`scenario state_change`) once ADR-004 is built; the Settings switch for remote control gives an A/B check if the run fails. Pending the owner's approval of ADR-004.
