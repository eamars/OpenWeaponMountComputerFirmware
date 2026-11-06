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

## Step 2: TE sync

### Measurement (2026-11-06, `41da7b5`, profiling build)

GPIO18 counts the TE pulse on both edges; the flush times each frame from its first strip queued to its last strip done (`te_prof` line, once a second). Display active on the digital level tile, Wi-Fi on (`build/rc_logs/rc_te_probe_20261106_125714.log`).

- TE pulses on GPIO18: 60-61 a second, period 16.5-16.9 ms, high about 0.56 ms (V-blank). The init sequence's `TEON` works as wired.
- Frame write time, average (max): 0° 21.8 ms (22.4); 90° 22.4 ms (23.4); 180° 21.8 ms (22.4); 270° 20.3 ms (20.8).
- So one frame takes longer than one panel scan and well under two. Starting the write on the TE falling edge (the scan starts at row 0) keeps the scan ahead of the write for the whole first refresh, and the second refresh would only meet the write pointer about 60 ms in, long after the write ends (≤ 23.4 ms). Every refresh then shows one whole frame. This assumes the panel scans from row 0 down like the write; the owner's eye confirms it.
- 10 minutes display active with the TE interrupt running (`rc_te_on_watch_20261106_130015.log`): 0 stalls. A stall 15 s after the post-flash boot on this build was the known flush-path stall (same SPI wait in the dump; the dump keeps no driver state, as the core dump saves task stacks only).

### Sync (2026-11-06, `a91987d`)

The flush waits for the TE falling edge before the first strip (binary semaphore given by the TE ISR; any edge that passed before the frame was ready is dropped first). Timeout 40 ms, counted in `te_timeouts`; on timeout the frame is sent anyway.

| Rotation | Frames/s | Wait avg (max) | Write avg (max) | Timeouts |
|---|---:|---:|---:|---:|
| 0° | 15-16 | 8.4 ms (16.7) | 21.2 ms (22.8) | 0 |
| 90° | 16 | 7.9 ms (16.8) | 22.2 ms (23.0) | 0 |
| 180° | 16 | 8.3 ms (16.8) | 21.1 ms (21.9) | 0 |
| 270° | 16-17 | 8.2 ms (16.7) | 20.3 ms (20.9) | 0 |

Before the sync: 17 frames/s at every rotation (same build otherwise). Cost: about one frame a second.

- `scenario state_change` (`rc_state_change_20261106_131438.log`): PASS, `taskLVGL` ≥ 2,048 B and `largest_dma` ≥ 80,000 B floors met, 0 TE timeouts across idle/wake, rotation saves and Wi-Fi off/on.
- 20 minutes of idle/wake cycles on the digital level tile (`rc_idle_cycles_20261106_131736.log`, 106 cycles; the owner caps cycle tests at 20 min): 0 stalls, 0 TE timeouts in 428 one-second windows, so the first frame after each wake finds its TE edge. `stats` 38 of 40 answered (console loss, seen before; `stats` takes no lock).

### Owner's check and final build (2026-11-06)

- Owner at the unit, digital level tile, all four rotations while moving it: "No tearing at all four".
- Final build: `DISPLAY_PROFILING_ENABLED 0`, `sdkconfig` regenerated from the defaults (trace facility off, task watchdog panic on). `scenario state_change --min-largest-dma 80000` (`rc_state_change_20261106_190815.log`): PASS, `largest_dma` 114,688 B throughout, two idle/wake transitions, rotation save, Wi-Fi off/on.

Step 2: **Built 2026-11-06.**
