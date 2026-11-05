# ADR-002 progress

What was built for [ADR-002](README.md) and what the device showed. Times are NZDT.

## Step 1: staging-buffer flush

### Starting point (2026-11-05)

Branch `display_pipeline` from `main` at `c327b08`, approved by the owner 2026-11-05.

Before the change (QSPI 4,092-byte transfers, LVGL 9.6, `esp_lvgl_port` 2.7.2):

| Reading | 180° | 90° |
|---|---:|---:|
| Frames/s | about 16 (15.8-16.8) | 12.9 |
| Flush callback average | 30.4 ms | 43.0 ms |
| Render average (includes the flush) | 54.5 ms | 66.3 ms |

Memory: `free_int` about 193 KB; `largest_dma` 114,483-114,688 B; `free_spiram` about 5,615,000 B; `taskLVGL` 8,328 B free of 16,384.

### The change (2026-11-05)

- New `main/display_flush.h` / `.c`; `main/lvgl_display.c` creates the display through it; `LVGL_DISPLAY_TRANS_SIZE` removed from `main/app_cfg.h`. No `lvgl_port_add_disp`, `lv_draw_sw_rotate` or `lv_draw_sw_rgb565_swap` left in `main/`.
- Pixel mapping checked on the host (a simulation of the port path with LVGL 9.6's rotate functions and `lvgl_port_rotate_area`, against the new strip loops; script kept outside the repo): 244 cases (full frame and 60 random rounded partial areas per rotation), identical framebuffer in every case, every strip window starts even and ends odd and fits the staging buffer.
- Rows per strip at 280 px: 28 (15,680 B; 17 strips per full frame), to keep the even-start rule.
- Details worth knowing: the source stride comes from `lv_draw_buf_width_to_stride` (equal to `w` with `CONFIG_LV_DRAW_BUF_STRIDE_ALIGN=1`); `LV_EVENT_FLUSH_FINISH` gets LVGL's logical area, not a physical one (the profiler uses only its timing); rotation is read with `lv_display_get_rotation` at flush time; failure to add callbacks counts as creation failure. Edge case: if a strip fails partway through `tx_color`, chunks already queued may still be on the bus when the next flush refills that buffer; only the dropped frame's area can show wrong pixels.
- Build: pass (`idf.py reconfigure` needed for the new source file). DIRAM 137,195 B. The only warning in the new files is LVGL 9.6's `LV_ASSERT_HANDLER_INCLUDE is deprecated`, which `lvgl.h` emits for every file (left for the IDF upgrade).

### Boot and memory (2026-11-05, Wi-Fi off, 330 s, rotation 180°)

- `Display created: draw_buf=2x255360 B (PSRAM) staging=2x16384 B (internal DMA)`; `display_prof_cfg ... staging_bytes=16384 ... qspi_actual_max_transfer=4092 ... rotation=2`.
- Heap (average after 10 s): `free_int` 198,028; `largest_int` 118,784; `min_int` 194,111; `free_dma` 190,288; `largest_dma` 118,784; `free_spiram` 5,875,115 (+260,115 B).
- Failure markers: 1 `rst:` (port open), 1 BNO085 first-boot abort (known); nothing else, no `alloc_failed:`. The one gap in `display_prof` lines matches `Entering idle mode` / `Exiting idle mode` exactly.
- Speed at 180° (81 windows with ≥ 10 flushes): 17.3 frames/s, render 42.4 ms, flush callback 19.2 ms (was 30.4 ms).

### Picture and speed (2026-11-05, owner at the device, Wi-Fi off)

Same boot, from uptime 330 s; rotation changes in the log: 2 → 0 (532 s) → 1 (550 s) → 2 (702 s). The owner kept the screen awake with the countdown timer.

- Owner: "All good now, no visible change" against the previous firmware (picture correct, no artefacts, tearing unchanged).

| Rotation | Windows | Frames/s | Render | Flush callback | Flush before |
|---|---:|---:|---:|---:|---:|
| 0° | 13 | 17.0 | 45,195 µs | 21,282 µs | not measured |
| 90° | 151 | 20.2 | 38,157 µs | 16,226 µs | 43.0 ms |
| 180° | 270 | 19.0 | 41,618 µs | 17,812 µs | 30.4 ms |

- Heap: `free_int` 198,068; `largest_dma` 118,784; `free_spiram` 5,874,798. Failure markers: none. Stacks: `taskLVGL` 8,136 B free; `sys_evt` 1,788 B free.

### 270° (2026-11-05)

- The owner set 270° and then 0° and reports both correct ("It is done already, 270 deg and 0 deg").
- The 270° selection fell between two captures (uptime 971-1,119 s) and is not in a log. The next capture starts at a rotation other than the last logged 180°: flush 15.7 ms, 21.7 frames/s over 121 windows, most likely 270°; then `Rotation updated to 1` and `to 0` two seconds apart; 0°: 181 windows, 19.9 frames/s, flush 18.3 ms. Failure markers: none.

### Stability runs (2026-11-05 to 2026-11-06)

- 35 minutes on the step 1 build (23:39-00:15, stopped at the owner's request to continue on the remote-control build, which carries step 1 unchanged): uptime 0-2,115 s, markers only the port-open reset and the BNO085 abort; `free_int` 198,085 average, min 195,411; `largest_dma` 118,784 throughout; `taskLVGL` 9,912 B and `sys_evt` 1,452 B free in all 7 stack reports; 10 idle/Wi-Fi transitions.
- Unattended `state_change` runs (Wi-Fi on and save, every tile, three idle/wake cycles, rotation 1 and 2 with save, Wi-Fi off/on; logs in `build/rc_logs/`): PASS on 2026-11-06 at 01:23, 09:41 and 12:23 (the last on the final build with the idle-entry fix). `rotation_sweep` (rotations 0-3, 120 s each, display active): PASS.
- More than 600 idle/wake cycles with display flushes on the final build: `largest_dma` 114,688 B in every reading; no `alloc_failed:`.
- Owner, 2026-11-06: no overnight soak; step 1 closes on these runs.
- Found on the way: the LVGL freeze (a display strip transfer that never completed while idle entry stopped Wi-Fi and changed the clock). The cause was in the low-power code, not the step 1 flush; fixed in `d8d78bf`. A rarer stall in the same SPI wait remains; see "Known issue in the flush path" in the README.

Step 1: **Built 2026-11-06.**
