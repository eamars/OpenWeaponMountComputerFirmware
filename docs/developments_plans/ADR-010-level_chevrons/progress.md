# ADR-010 progress

## Step 1: the chevron style (2026-11-08, with the owner on the unit)

Each version was built, flashed and shown on the owner's unit. The figures come from `rc_run.py scenario level_motion` (new in ADR-009): 30 one-second windows while the owner rocked the unit, on builds with `DISPLAY_PROFILING_ENABLED 1`. "Drawing" is the render time less the flush. The flush is the copy to the panel plus its wait for the panel's scan.

| Version | Frames a second (median) | Drawing a frame | Flush a frame | Pixels a frame | Log |
|---|---:|---:|---:|---:|---|
| Thick anti-aliased lines, band of half the screen | 26 | about 18 ms | about 16 ms | about 65,000 | `rc_level_motion_20261108_172421.log` (`rc_send_20261108_172525.log`) |
| Pre-rendered tile, band of half the screen | 29 | 10.8 ms | 21.8 ms | 66,000 | `rc_level_motion_20261108_173511.log` |
| Tile, band between the overlays, anti-tearing on | 22 | 9.2 ms | 27.7 ms | 62,600 | `rc_level_motion_20261108_174155.log` |
| Same, anti-tearing off | 43 | 8.6 ms | 10.3 ms | 58,000 | `rc_level_motion_20261108_174241.log` |
| Tile, full screen (chosen) | 17 (13 to 20; 6 in the windows crossing level) | 18.9 ms | 30.6 ms | 121,800 | `rc_level_motion_20261108_175229.log` |

### What changed, and why

1. **First build:** two green bars when level, and chevrons drawn as thick anti-aliased lines with round ends, over half the screen.
   - The owner: the bars were not obvious enough, and the chevrons were not responsive.
   - Measured: the level loop got the LVGL lock for only about 6 of 50 reports a second (it waits 1 ms; the view kept the LVGL task 88% busy).
   - Changes: the level fills the view with its colour. The animation timer reads the newest angle itself every frame. The speed range was widened (0.3 to 3 periods a second over 3°, was 0.6 to 3 over 4°).
2. **Pre-rendered tile:**
   - Asked by the owner after research into LVGL's cheapest path. In the LVGL 9.6 source, thick slanted lines go through four line masks per row (`draw_line_skew`), while an opaque RGB565 image without a mask is a `lv_memcpy` per row (`rgb565_image_blend`). An image widget tiles one period (`LV_IMAGE_ALIGN_TILE`) and moves it with `lv_image_set_offset_x`.
   - With it came the owner's hazard-tape shape: equal stripe and gap, 45°.
   - The angle pill now updates from the animation timer: through the level loop it had fallen to about 1.5 updates a second.
3. **Band between the overlays:** the owner's first option. The band filled the rows between the angle pill and the timer bar or dope, so no text redrew with it.
4. **Anti-tearing off (step 2):** twice the frame rate, but the owner saw tearing. Kept on.
5. **80% and then full screen:** the owner's second option. With darker tilt colours chosen by the owner, the text stays readable over the chevrons and no tearing was seen. Full screen chosen. It is the slowest version (17 frames a second), and the owner judged it the best.
6. **Tile rendering:** the first full-screen build failed `state_change` with "save system: busy" after a rotation change.
   - Re-rendering the tiles used `fmodf` per sample and held the LVGL lock for about a second.
   - Rewritten with whole numbers and a running wrap: 138 ms in landscape, 367 ms in portrait (logged at each render).
   - `state_change --min-largest-dma 70000` (`rc_state_change_20261108_175911.log`): PASS, `largest_dma` 81,920 B at least.

### Not done

- The owner has not yet confirmed by hand that the chevrons point the way to turn in both directions. The captures show orange (right tilt colour) chevrons pointing left and blue (left tilt colour) pointing right.
- Crossing the level redraws the whole screen in one frame (about 50 ms). It is seen as a short drop in the frame rate.

## Step 2: anti-tearing off

Tried and rejected; see the table and the README.
