# ADR-018 progress

COM13 (`owmc-3861330`), 2026-11-13, over USB, flashed with `idf.py flash`. Its card: targets pulled from the owner's Kestrel (ranges and gusts known), more than five of them set. Snaps and logs: `logs/rc/snaps/adr018_*` and `logs/rc/rc_adr018_*` (not tracked).

## Built

- **One current target.** The position, the search for set targets and the stepping moved from `level_band.c` to `dope_view.c` (`dope_current_target`, `dope_find_set_target`, `dope_step_target`), with one listener (`dope_set_card_listener`) that the foreground showing the card registers when it is built and clears when it is deleted. The band and the recoil step in the level's reader use them.
- **The Dope foreground** (`level_card.c`): five rows in portrait, four columns in landscape, in the same fonts. Its widget table is in PSRAM.
- **The Level page:** Foreground lists Match, Dope, Off. Dope is stored as 2; with it the level tile offers the dope tile and not the timer tile.
- `scenario layer_tour` walks the three foregrounds.
- Build: no warning from our code; `dependencies.lock` unchanged. The image is 1,852 bytes larger than ADR-017's.

## The trial (the owner's choice)

Three layouts were on the unit, switched by a remote control command that is now removed:

| Layout | Portrait | Landscape | Outcome |
|---|---|---|---|
| 0 | five rows, three columns each, elevation 48 px | three rows | portrait kept |
| 1 | four rows of two lines, elevation in the 64 px digit font, range small | two rows | dropped |
| 2 | as 0 | four targets side by side (the owner's question: use the width) | kept |

The owner: "Keep" (layout 2).

## Measured (the final build)

- **The widths, from the font file** (Montserrat Medium, as LVGL's): "1000" at 28 px is 67 px and its "m" 21; "12.5" is 85 px at 48 px and 113 at 64; "99.9" is 101 px at 48; "L1.2" is 49 px at 28.
- **Match is unchanged:** the level with Match over Block, in portrait and landscape, is the same picture, pixel for pixel, as on the ADR-017 build.
- **Stepping:** in portrait five taps move the outline from target 1 to 6 and the rows follow (targets 2 to 6 shown, the mark moved down); a hold goes back to 5. In landscape the same along the columns (targets 3 to 6 shown, the mark along the bottom). Match then shows the same target.
- **The swipes with Dope:** up goes to the dope tile, left to the settings; down leaves the level tile and the card is the same picture before and after.
- **`scenario layer_tour --min-largest-dma 70000`: PASS**, three rounds of the twelve pairs: free internal RAM 161,991 at the start and at the end; the largest DMA block 81,920 throughout.
- **`scenario state_change --min-largest-dma 70000`: PASS** (largest DMA block 81,920 awake, 73,728 while idle, as before).
- **Memory after a reboot:** free internal 162,079, largest DMA block 81,920 (the ADR-017 build: 162,111 and 81,920). Static internal RAM is 48 bytes larger than ADR-017's: the foreground's few pointers and the card's position.
- One snap of the proof (landscape after five taps) came back incomplete over USB; the same state was snapped whole on the trial build (`adr018_columns`).

## What building found

- **A swipe that leads nowhere was a tap.** With Dope the level tile does not offer the timer tile, so a swipe toward it scrolls nothing, and LVGL ends it as a click on the card: the current target moved on. The card now takes a tap or a hold only from a touch that ends within 20 px of where it began. Seen and fixed on the unit.
- **The Level background's edge ticks and the card.** The two white ticks at mid-height sit where text is. In the dropped four-row layout the left tick landed right before an elevation and read as a minus sign. In the kept layout they touch only ranges: under the second row's range in portrait, beside the first and last columns' ranges in landscape. A mark beside a range cannot be read as a wrong hold, so nothing was changed.
- **The small orange name and the grey "m" and gust are faint on the light green** of a levelled background in the snaps. The white numbers read. For the owner's eye on the panel.
- The unit's Level settings have been saved by this firmware (48 bytes): firmware from before ADR-017 would reset them, the zero included.

## Found on the unit: a reboot from one failed touch read (step 4)

During the trial the unit rebooted in the owner's hand (2026-11-13, 18:3x).

- **What happened:** reset reason PANIC, task `taskLVGL`. The stored core dump: `ESP_ERROR_CHECK` failed with `0x108` (`ESP_ERR_INVALID_RESPONSE`: the touch controller answered an I2C read with a NACK) in esp_lvgl_port's touch read callback (`esp_lvgl_port_touch.c:127`).
- **Why:** esp_lvgl_port aborts on any error from the touch driver's read, and our FT3168 driver returned the I2C error. ADR-005 C3 gave the read a finite timeout "so a stuck bus is a missed touch", but the error was still returned, so it was a reboot. Every firmware since has this; no earlier log shows it happening.
- **The fix** (`components/esp_lcd_touch_ft3168`): a failed read is logged, counted and reported as no touch.
- **Proof on COM13,** with a temporary switch that fails the next read (not kept): with the fix, three failed reads gave three "Touch read failed (ESP_ERR_INVALID_RESPONSE), taken as no touch" lines and the unit carried on; with the old behaviour, one failed read gave the same abort at the same file and line, and a reboot.
- **Not found:** why the controller NACKed that read. The dump and both ELF files are in `logs/crash_20261113/`.
- The owner kept the fix in this ADR (2026-11-13), not in a change of its own.

## The owner's check

The owner checked the final build on COM13 by hand on 2026-11-13, with an edit on the dope tile and a card of fewer targets than fit asked for: "Checked, it works."
