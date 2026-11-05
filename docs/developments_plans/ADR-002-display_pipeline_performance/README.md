# ADR-002: display pipeline performance, one change at a time

Status: **Open.** Step 1 Built (2026-11-06). Step 2 next, step 3 last; both are proposals until the owner approves them.

## What we want

A faster, smooth display, paid for with the internal SRAM that moving memory to PSRAM freed. Each change is added alone and proven on the device before the next one starts, because display performance changes have caused runtime instability before.

## Owner's direction

1. Performance changes are risky. One at a time, each proven on the device before the next.
2. Tearing was diagonal ("everything morphed 45°"). The code around tearing is deliberate: the digital level view redraws the full screen, and `full_refresh = false`. Don't change it to chase speed.
3. Redrawing only the changed part of the digital level view comes last.
4. QSPI transfers stay at 4091 B (`LCD_QSPI_MAX_TRANSFER_SIZE`, one 4,092-byte DMA descriptor). It is intentional, for DMA headroom.
5. No long soak runs unless a specific failure needs one.

## How the display works today

- Panel: SH8601 AMOLED, 280 × 456, QSPI at 40 MHz on SPI2 (the only device on that bus). The driver has no hardware rotation, so 90°, 180° and 270° are done in software.
- LVGL 9.6 renders into two full-frame RGB565 draw buffers in PSRAM (partial render mode).
- The project owns the flush (`main/display_flush.c`, since step 1): each frame is rotated and byte-swapped strip by strip from PSRAM into one of two internal DMA staging buffers (2 × 16,384 B, 28 rows per strip at 280 px, so 17 strips per full frame) and sent with `esp_lcd_panel_draw_bitmap`. The next strip is prepared while the previous one is on the bus.
- A full frame is 255,360 B; bus time alone is about 12.8 ms.
- The panel's tearing-effect output is switched on in the init sequence (`0x35` TEON, parameter `0x00`, V-blank only, `main/bsp.c`) and wired to GPIO18 (`LCD_TE_OUT` in `main/app_cfg.h`). The firmware does not read it yet.

Facts the flush relies on, worth knowing before touching it:

- The SH8601 needs every window to start on an even row and end on an odd row; strips keep that.
- ESP-IDF's SPI panel IO waits for all in-flight transfers before sending the next window command, so a staging buffer is free again once the following `draw_bitmap` call has returned.
- The flush-done callback runs in the SPI interrupt. It counts strips and calls `lv_display_flush_ready` after the last one; it must not block, allocate or log.

## Step 1: fixed internal staging buffers (Built 2026-11-06)

Why: before this step, every frame went through three full passes over PSRAM (rotate into a 255 KB rotation buffer, byte-swap it in place, then the SPI driver copied each 4 KB chunk into a freshly allocated internal DMA buffer). At 90° the rotation read PSRAM column by column, which made landscape the slowest.

What was built: `main/display_flush.c` creates the LVGL display itself, in place of `esp_lvgl_port`'s `lvgl_port_add_disp` (the port still provides the LVGL task, lock, touch and stop/resume). One PSRAM read pass per frame, written rotated and swapped straight into the staging buffers; no per-transfer allocation; the rotation buffer is gone (+260 KB PSRAM). The pixel mapping is the same as the port's, checked by a host simulation over 244 cases.

Measured (flush callback average, frames per second):

| Rotation | Before | After |
|---|---|---|
| 0° | not measured | 21.3 ms, 17.0 fps |
| 90° | 43.0 ms, 12.9 fps | 16.2 ms, 20.2 fps |
| 180° | 30.4 ms, about 16 fps | 17.8 ms, 19.0 fps |
| 270° | not measured | about 15.7 ms, 21.7 fps (rotation attributed by elimination) |

The owner checked the picture at all four rotations: correct, tearing unchanged. Memory: the largest internal DMA-capable block stayed at 114,688-118,784 B in every run. Evidence and run details: [progress.md](progress.md).

## Step 2: start each frame on the panel's tearing-effect pulse (proposal)

Why: the write order relative to the panel's scan is random today, which is what makes tearing possible. Starting each frame's transfer on the TE pulse makes it deterministic. That is the precondition for step 3 (partial redraw without tearing).

What we know: the hardware path is checked on the schematic (`OpenWeaponMountComputer/pcb/owmc.kicad_sch` at `dce2f25`): module pin 27 `TE (OUT)` → connector J1 pin 27, net `AMOLED_TE_OUT` → ESP32-S3 GPIO18, nothing else on the net. Not yet known: whether TE actually pulses on GPIO18, at what rate, and how long one frame write takes against one panel refresh.

Approach:

1. Measure first, change nothing visible: count GPIO18 edges and time them; report the TE rate next to the frame rate. If there are no pulses, stop and look at the init sequence before anything else.
2. Then sync: in the flush, wait for the next TE edge before sending the first strip of a frame. The wait has a timeout of a couple of TE periods and is counted when it expires, so a missing pulse (for example with the panel off) can never hang the display.
3. Judge on the device: the owner's eye at all four rotations, the frame rate (waiting for TE may cost up to one panel refresh per frame), and the usual idle/wake and state-change runs for stability.

## Step 3: partial redraw of the digital level view (last)

Why: drawing is about 24 ms of each frame (render about 42 ms at 180°, of which about 18 ms is the flush), and nearly all of it redraws pixels that did not change. Invalidating only the changed regions is the largest expected gain.

Only after step 2 is Built and tearing is handled by the TE sync rather than by full redraws. Judged by the owner's eye for the diagonal tear, plus the frame rate and invalidated pixels per frame.

## Known issue in the flush path

A display strip transfer occasionally never completes: the LVGL task then waits forever in `spi_device_acquire_bus` (from `display_flush_cb`) with the LVGL lock held, and the LVGL watchdog reboots the unit after 5 s (about 9 s until the UI is back).

- One cause was found and fixed on 2026-11-06 (`d8d78bf`): idle entry ran inside an LVGL timer pass, so a frame was still being flushed while Wi-Fi stopped and the clock config changed. Idle entry and wake now run in the low power task after LVGL has stopped and the panel has drained.
- A rarer stall remains: 1 in about 630 idle/wake cycles on the fixed code, seen 0.4 s after a wake during the Wi-Fi scan, mid-frame. The clock change cannot explain that one (it ran with the panel drained). Cause unknown.
- Next evidence: a core dump copied straight from flash (`read_flash 0x12000 0x20000` before anything else; reading it with `coredump-info` resets the unit, and the next boot's BNO085 abort overwrites it). The SPI driver state in it tells a transfer the hardware never finished (`cur_cs` set) from a completion the driver lost (`cur_cs` none with requests queued).

## Ideas set aside

QSPI clock above 40 MHz, larger QSPI transfers, internal LVGL draw buffers, more LVGL draw units. Worth revisiting only if a measurement shows the bus or the copy is the remaining limit.
