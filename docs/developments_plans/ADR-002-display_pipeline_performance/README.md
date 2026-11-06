# ADR-002: display pipeline performance, one change at a time

Status: **Built 2026-11-06.** Step 1 Built (2026-11-06). Step 2 Built (2026-11-06). Step 3 Built (2026-11-06).

ADR-002 is functional change without any dependency update (ESP-IDF v5.5.2 and today's managed components stay as they are). ADR-003 is the dependency update without functional change. The two never cross over: one is finished before the other starts (owner, 2026-11-06).

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

## Step 2: start each frame on the panel's tearing-effect pulse (Built 2026-11-06)

Why: the write order relative to the panel's scan is random today, which is what makes tearing possible. Starting each frame's transfer on the TE pulse makes it deterministic. That is the precondition for step 3 (partial redraw without tearing).

What we know: the hardware path is checked on the schematic (`OpenWeaponMountComputer/pcb/owmc.kicad_sch` at `dce2f25`): module pin 27 `TE (OUT)` → connector J1 pin 27, net `AMOLED_TE_OUT` → ESP32-S3 GPIO18, nothing else on the net. Not yet known: whether TE actually pulses on GPIO18, at what rate, and how long one frame write takes against one panel refresh.

Approach:

1. Measure first, change nothing visible: count GPIO18 edges and time them; report the TE rate next to the frame rate. If there are no pulses, stop and look at the init sequence before anything else.
2. Then sync: in the flush, wait for the next TE edge before sending the first strip of a frame. The wait has a timeout of a couple of TE periods and is counted when it expires, so a missing pulse (for example with the panel off) can never hang the display.
3. Judge on the device: the owner's eye at all four rotations, the frame rate (waiting for TE may cost up to one panel refresh per frame), and the usual idle/wake and state-change runs for stability.

What was built: TE pulses on GPIO18 at about 60 Hz (period 16.5-16.9 ms); one frame write takes 20-23 ms, more than one panel refresh and well under two. `main/display_flush.c` waits for the TE falling edge before the first strip of each frame (40 ms timeout, counted; the frame is sent anyway). The owner saw no tearing at any of the four rotations. Cost: about one frame a second (17 to 16 frames/s). Details in [progress](progress.md).

## Step 3: partial redraw of the digital level view (Built 2026-11-06)

Why: the level view redraws the whole screen for every frame, although between two frames only a band of rows changes. Drawing and sending fewer pixels is the largest gain left, and the level reads smoother at a higher frame rate.

What we know (step 2 profiling build, digital level tile, 0°, `rc_te_on_watch_20261106_130015.log`):

- Every frame is a full flush: 127,680 px (280 × 456), 17 a second. A frame takes about 45 ms to render, of which about 21 ms is the flush.
- The level is updated every 20 ms (`DIGITAL_LEVEL_VIEW_DISPLAY_UPDATE_PERIOD_MS`, 50 Hz), so today about two of every three sensor updates never reach the screen.
- The default style (`digital_level_view_type_1.c`) calls `lv_obj_invalidate` on the whole view on every update. The picture is a background colour, a triangle of rows `[base - dy, base + dy]` across the full width, a foreground rectangle from there to the bottom, and two 8 px level lines at `base`. Between two updates only the rows between the old and new triangle edges and the old and new line positions change. The whole screen changes only when the background colour changes (tilt left, level, tilt right). Style 2 already changes only its three objects, and LVGL redraws only those.
- The catch: step 2's rule of starting on the TE falling edge is tear-free only for areas that begin at the top of the panel. The panel scans at most about 36.6 µs per row (TE period / 456), top-down as the owner's check in step 2 bears out, and the flush writes about 47.8 µs per row, so the scan is faster. For a full frame, the scan starts with the write at row 0 and stays ahead of it. For a band starting lower, at physical row `y0`, the write starts ahead of the scan, and the scan catches it inside the band whenever the band is taller than about 3 × `y0` rows, for example a band at row 50 taller than about 160 rows. That refresh shows the band half old and half new, which is a tear.

Approach, built in this order, as one step:

1. Flush: start each area at a point of the scan where the scan cannot cross the write (`display_flush_wait_scan` in `main/display_flush.c`). The write's speed matters as well as its first row, which the proposal first missed. Full-width rows are written slower than the panel scans them, so they start right behind the scan, once it has passed the area's first physical row `y0`. Narrow areas are written faster than the scan: the angle label, and every band at 90° and 270°, which is a full-height panel column there. Starting right behind the scan, such a write would overtake it inside the area. So it starts far enough behind that it reaches the area's last row only after the scan has. One formula covers both. The scan takes `s` per row (TE period / 456, which can only overstate it), and the write `v` per row (`pw` px at 150-190 ns/px around the measured 159-183). The write starts when the scan is `h × max(0, s − v_min)` rows' worth past `y0`. Starting late is safe for up to `P − h × (max(0, s − v_min) + max(0, v_max − s))` minus 1 ms of margin (P the TE period); past that, the next pass would cross the write, so the flush waits one more pass, at most once. A full frame starts on the TE falling edge exactly as in step 2. The ISR records the edge times, and a one-shot timer wakes the flush. The first strip is filled before the wait. Each area LVGL sends gets the rule on its own, so the angle label and the band may land one refresh (16.7 ms) apart. The label is only set when its number changes.
2. Level view, style 1: invalidate only the band of rows that changed (the union of the old and new triangle and the lines, with the 4 px line half-width), and the whole view when the background colour changes. The angle label, countdown and dope card above it are already their own objects, so LVGL redraws them as before.
3. Judge on the device: the owner's eye at all four rotations while moving the unit, at small and large tilt (no tear, no stale stripes left behind by the band), the frame rate and invalidated pixels per frame from `display_prof`, then `state_change` and one idle/wake cycle test of at most 20 minutes.

Expected, an estimate until measured: at a few degrees of tilt the band is tens of rows, under 15% of the screen, so a frame should take a few milliseconds and the frame rate should rise towards the 50 Hz sensor update rate. A colour change still costs one full frame, as today.

Watch: about three times as many flushes a second. If the known stall below happens per transfer, it may show more often; the cycle test would show it.

No dependency changes; LVGL 9.6 and `esp_lvgl_port` 2.7.2 stay as they are.

What was built: the scan-safe start per area and the changed-rows invalidation above. At 90° and 270°, a thin band's safe window is about as long as its ~1 ms write, and the LVGL task often wakes later. A late start therefore waits one more pass only when the window is at least 2 ms; otherwise it is sent at once, so part of a thin band may show one refresh late (owner approved). Measured near level at 0°: up to 56 flushes a second of about 6,800 px each, against 17 full frames of 127,680 px before; a still unit draws nothing. The owner saw no tearing at any rotation, including 90° and 270° after the retry change. Details in [progress](progress.md).

## Known issue in the flush path

A display strip transfer occasionally never completes: the LVGL task then waits forever in `spi_device_acquire_bus` (from `display_flush_cb`) with the LVGL lock held, and the LVGL watchdog reboots the unit after 5 s (about 9 s until the UI is back).

- One cause was found and fixed on 2026-11-06 (`d8d78bf`): idle entry ran inside an LVGL timer pass, so a frame was still being flushed while Wi-Fi stopped and the clock config changed. Idle entry and wake now run in the low power task after LVGL has stopped and the panel has drained.
- A rarer stall remains: 1 in about 630 idle/wake cycles on the fixed code, seen 0.4 s after a wake during the Wi-Fi scan, mid-frame. The clock change cannot explain that one (it ran with the panel drained). Cause unknown.
- A fix here stays within ADR-002's rule: no dependency update. If the cause turns out to be in ESP-IDF itself, it is worked around in this firmware or left for ADR-003 to bring in, never patched in by updating a dependency here.
- Next evidence: a core dump copied straight from flash (`read_flash 0x12000 0x20000` before anything else; reading it with `coredump-info` resets the unit, and the next boot's BNO085 abort overwrites it). The SPI driver state in it tells a transfer the hardware never finished (`cur_cs` set) from a completion the driver lost (`cur_cs` none with requests queued).

## Ideas set aside

QSPI clock above 40 MHz, larger QSPI transfers, internal LVGL draw buffers, more LVGL draw units. Worth revisiting only if a measurement shows the bus or the copy is the remaining limit.
