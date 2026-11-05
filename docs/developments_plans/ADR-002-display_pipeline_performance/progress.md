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
