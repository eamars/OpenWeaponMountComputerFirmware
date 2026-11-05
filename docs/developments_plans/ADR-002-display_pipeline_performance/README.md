# adr-002: display pipeline performance, one change at a time

## Summary

- Goal: raise the display frame rate by spending the internal SRAM freed in ADR-001 on the display driver, adding one performance change at a time.
- Plan class: medium (per step)
- Status: draft (2026-11-05). Not executable. Each step becomes its own executable plan section, approved by the owner, only after the step before it is Built.
- Mandatory skills: `esp32-debug`, `development-plan`
- Overall cutover strategy: one step at a time. A step that fails its device gate is reverted before the next step starts.
- Highest-risk areas: replacing the `esp_lvgl_port` flush path; tearing; runtime instability that appears only under Wi-Fi load.
- Acceptance criteria: defined per step when the step is finalized.

## Owner's direction (2026-11-05)

1. Performance changes are risky (unpredictable runtime instability). Add them one at a time, each proven on the device before the next.
2. Tearing was diagonal ("everything morphed 45°"). What is in the code around tearing is deliberate: the digital level view's full-screen redraw and `full_refresh = false`.
3. Redrawing only the changed part of the digital level view comes last.

## Context

Measured on COM11, 2026-11-05 (branch `spiram_optimization` at `c6f2c63`, QSPI 4096 → actual 8,184 B, Wi-Fi off): 17 frames/s, every frame full-screen (127,680 px). Per frame, about 24 ms is LVGL drawing into the PSRAM draw buffer and about 25 ms is the flush callback. LVGL waits only about 7 µs for the previous flush, so the frame is serial: draw, then flush.

The flush callback (`esp_lvgl_port` 2.7.1, `lvgl_port_flush_callback`) does, for rotation 180°:

1. `lv_draw_sw_rotate` of the whole frame, PSRAM draw buffer → PSRAM rotation buffer (`draw_buffs[2]`, 255,360 B).
2. `esp_lcd_panel_draw_bitmap`, which queues SPI transactions of up to the bus max transaction length. For each transaction the SPI master allocates an internal DMA buffer and copies the chunk from PSRAM into it (`spi_master.c`, `setup_priv_desc`), with up to 10 queued.

Bus time alone is about 12.8 ms per frame (255,360 B at 40 MHz × 4 lines). The rest of the flush is the two full-frame copies and per-chunk allocation. An earlier trial raised the transaction size from 4,092 to 8,184 B: flush time fell from 30.3 to 25.3 ms and the frame rate did not change (`references/display-refresh-rate-rca.md`).

Hardware notes: SH8601 supports neither hardware `swap_xy` nor `mirror_y` in this driver (`esp_lcd_sh8601.c`), so 90°/180°/270° stay in software. The panel's tearing-effect output is enabled by init command `0x35`; `LCD_TE_OUT` (GPIO18) is defined in `main/app_cfg.h` and not read by the firmware.

## Steps, in order

Each step lists what it changes, why, and the gate it must pass. Thresholds are fixed when the step is finalized from the numbers ADR-001 leaves behind.

### Step 1: fixed internal staging buffers, rotate straight into DMA memory

- Change: the project owns the flush. Two DMA-capable internal staging buffers, allocated once at display init (size fixed at finalization; 2 × 16 KB is the starting point). For each strip of the output frame, rotate from the PSRAM draw buffer directly into a free staging buffer and send it with `esp_lcd_panel_draw_bitmap`; rotate the next strip while the previous one is on the bus. Signal LVGL `lv_display_flush_ready` when the last strip completes. The full-frame rotation buffer is no longer allocated.
- Why: one copy per frame instead of two; no per-transaction allocation by the SPI master (staging buffers are DMA-capable and aligned), so display DMA memory is fixed and does not fragment the heap or compete with Wi-Fi; 255,360 B of PSRAM freed; rotation overlaps the bus.
- Tearing: the panel still receives one continuous frame in the same row order as today, so tearing behaviour is expected to be unchanged. This is checked on the device, not assumed.
- Gate: device run with Wi-Fi connected, no failure markers, flush time and frame rate recorded, owner's visual check of tearing at all four rotations.

### Step 2: tearing-effect sync on GPIO18

- Change: start each frame's transfer on the panel's TE pulse.
- Why: makes the write order relative to the panel scan deterministic, which is the precondition for any partial update without tearing.
- Gate: owner's visual check at all rotations; frame rate recorded.

### Step 3: partial redraw of the digital level view (last)

- Change: invalidate only the regions that change instead of the whole level object.
- Why: removes most of the 24 ms draw and most bus time per frame; this is the largest expected gain.
- Precondition: Step 2 is Built and tearing is fixed by sync, not by full redraws.
- Gate: owner's visual check for the diagonal tear; frame rate and invalidated pixels per frame recorded.

## Deferred

- QSPI clock above 40 MHz, larger QSPI transactions, internal LVGL draw buffers, LVGL draw-unit count. Reconsider only with a measurement after Step 1 that shows the bus or the copy is the remaining limit.

## Open questions (draft only)

- Step 1 staging buffer size and strip height: fixed from ADR-001's largest free DMA block with Wi-Fi connected.
- Step 1 ownership: a project flush callback replaces `esp_lvgl_port`'s flush for this display only, without patching the managed component. The exact interface (where the panel IO "color transfer done" callback is registered) is settled when Step 1 is finalized.
