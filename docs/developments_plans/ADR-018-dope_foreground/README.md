# ADR-018: a Dope foreground, the card as rows

Status: **Built** 2026-11-13. The owner chose it as the next ADR ("018 first"), settled its questions and chose the layout on the unit the same day. Built and proven on COM13 ([progress](progress.md)), and checked by the owner by hand on the final build ("Checked, it works").

## What we want

A foreground for shooting without a clock: field shooting, or hunting with a fixed card. It shows several targets of the dope card at once, like a wrist coach or the Kestrel HUD's card screen, with no timer.

Done means: the owner picks "Dope" on the Level page's Foreground row, the card reads at a glance in portrait and landscape, and the owner has checked it by hand on the unit.

## Owner's direction (2026-11-13)

- "DOPE (pure dope, like Kestrel HUD)", showing more dope rows.
- For field shooting without a timer, or hunting with static dope.
- The unit has no ballistic solver. A range is shown only when the Kestrel gave it: no range entered by hand next to a hold the unit cannot check ("no information is better than misleading information").
- Equal targets with one highlighted, to try first. The other design (one large current target with small ones for what comes next) is kept as the fallback if this one does not work in use.
- A target leads with its range when the Kestrel gave one, the name small beside it; a hand-entered target leads with its name.
- The targets shown follow the highlighted one. No scrolling by finger.
- No backing behind the text to begin with.
- In landscape, use the width: the targets side by side as columns, not rows.
- The layout, chosen on the unit from three ("Keep"): five rows in portrait, four columns in landscape.

## What we know

- **The card** (ADR-007): 10, 12 or 15 targets, each with elevation, wind, an optional gust and an optional range, in mil and metres. Hand entry gives elevation and wind; range and gust come only from a Kestrel pull.
- **Match shows one target** in the band, its elevation in the largest digits on the screen (the 84 px digit font in portrait, 64 px in landscape). Without an armed timer the band already shows dope only, so Dope must offer what Match does not: more than one target at a glance.
- **The foreground slot** (ADR-017): a foreground is built when chosen and deleted when another is, says which of the level tile's neighbours it uses, and is checked by `scenario layer_tour`. Its stored values are Match 0 and Off 1.
- **The current target** was a file static of the band (`level_band.c`), RAM only, with the stepping (tap, hold, recoil) beside it. A second foreground that steps through the same card needs it in one place.
- **The room on the screen:** 280 x 456 px. The tilt box takes the top 66 px; with 20 px under it and 8 px at the bottom, 362 px are left in portrait and 186 px in landscape.
- **The widths, measured from the font file.** A portrait row is 252 px wide inside its margins. A long-range target needs four digits of range and a two-digit elevation ("1000 m", "12.5", "L1.2"): side by side they fit with the elevation in the 48 px font, and not with 64 px digits. Stacked as a column, the same fonts need 101 px at most ("99.9"), so four targets fit across the 444 px of a landscape screen; rows fit three there.

## What we do

1. **One current target for every foreground.** The position and the stepping move from the band into the card's module (`dope_view.c`): the current target, next and previous, and one listener that the foreground showing the card registers. The band shows what it showed before.

2. **The Dope foreground** (`level_card.c`):
   - **Portrait: five rows.** Each row has three columns: the range (the name small above it), the elevation, the wind (the gust small under it). The elevations' right edges are in line, so the decimal points line up.
   - **Landscape: four columns.** Each target is a column: name, range, elevation, wind, gust.
   - **The same fonts in both rotations:** range 28 px, elevation 48 px, wind 28 px, name and gust 20 px.
   - The card's set targets, in the card's order. An empty card shows nothing but the tilt box, as Match does.
   - A pulled target leads with its range; a hand-entered target with its name, in the range's size. Mil, as the rest of the unit. Up is plain and down has a minus, as on the dope tile.
   - The current target has a white outline, the tilt box's own style, which reads on every background colour. A tap anywhere under the tilt box moves it to the next target and a hold to the previous, going round the card. Recoil moves it when the Dope page's setting is on, and stops at the last target.
   - The targets shown move just far enough to keep the current one among them. A mark shows where they are in the card when the card has more than fit: down the right edge in portrait, along the bottom in landscape.
   - White text with no backing, over whichever background is chosen.

3. **The Level page:** Foreground becomes Match, Dope, Off. Dope is stored as value 2, after the two that exist, so the saved settings keep their meaning. With Dope chosen the level tile offers the dope tile (hand entry, the Kestrel pull) and not the timer tile.

4. **A failed touch read is a missed touch, not a reboot** (found on the unit during the trial; kept in this ADR by the owner's decision, 2026-11-13). The unit rebooted in the owner's hand: the touch controller answered one I2C read with a NACK, our FT3168 driver returned the error, and esp_lvgl_port's read callback aborts on any error from it. The driver (`components/esp_lcd_touch_ft3168`) now logs and counts a failed read and reports no touch. Every firmware since ADR-005 had this, v1.4.1 included.

## Tried and dropped (on the unit, 2026-11-13)

- **Four rows with 64 px digits** (two in landscape): the larger elevation costs the range its size, which the owner wanted to lead; and the Level background's left tick landed right before the elevation, so "1.1" read as "-1.1".
- **Three rows in landscape:** one target fewer than columns, with the middle of the screen empty.

## How the unit shows it works

On COM13, over USB ([progress](progress.md)):

- **The layout:** snaps in portrait and landscape over each background, with the owner's card.
- **Match is unchanged:** the level with Match is the same picture before and after; tap, hold and recoil step one position shared with Dope.
- **Stepping:** tap and hold go round the card and the shown targets follow, in both rotations.
- **The swipes:** with Dope, up to the dope tile works; down to the timer tile does nothing, and does not step the card.
- **Memory:** `scenario layer_tour --min-largest-dma 70000` with Dope in the round passes (free internal RAM back to where it started); free internal RAM after a reboot no lower than ADR-017's on the same unit beyond the foreground's few statics; `scenario state_change --min-largest-dma 70000` passes.
- **The touch read:** with a failed read injected, the unit logs it and carries on; with the driver as it was, the same injection gives the abort the owner saw.
- **The owner's check by hand,** which includes what the remote control cannot do without changing the owner's card: an edit on the dope tile showing in the rows, and a card with fewer targets than fit.

## Not in this ADR

- Range by hand, and any ballistics on the unit.
- Imperial units.
- The live Kestrel target (ADR-020).
- A backing behind the text: not needed in the snaps; the owner judges the small orange names and the grey "m" on the panel.
