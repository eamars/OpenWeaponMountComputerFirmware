# ADR-018: a DOPE foreground, the card as rows

Status: **Draft** 2026-11-13. Not approved. Needs ADR-017 (the foreground slot).

## What we want

A foreground for shooting without a clock: field shooting, or hunting with a fixed card. It shows several targets of the dope card at once, like a wrist coach or the Kestrel HUD's card screen, with no timer.

## Owner's direction (2026-11-13)

- "DOPE (pure dope, like Kestrel HUD)", showing more dope rows; open to suggestions.
- DOPE is for field shooting without a timer, or hunting with static dope.
- The unit has no ballistic solver. A range is shown only when the Kestrel gave it: no range entered by hand next to a hold the unit cannot check ("no information is better than misleading information").

## What we know

- **The card today** (ADR-007): 10, 12 or 15 targets, each with elevation, wind, an optional gust and an optional range, in mil and metres. Hand entry gives elevation and wind; range and gust come only from a Kestrel pull.
- **Match shows one target** in the band: the name, the range and the next target, then elevation and wind in the largest digits on the screen. Without an armed timer the band already shows dope only, so DOPE must offer something Match does not: more than one target at a glance.
- **The Kestrel HUD** has three screens: a single target, the target card, and DOPE (the Kestrel library's notes on the HUD link).
- **The screen:** 280 x 456 px. The tilt box takes the top; text keeps out of the 30 px corners.
- **The current target** is a file static of the band (`level_band.c`), RAM only. A second foreground that steps through the same card needs it in one place.

## What we propose

- **Rows of the card,** in the card's order, set targets only. About five rows in portrait and three in landscape; the exact sizes come from mock-ups in both rotations, shown to the owner before code.
- **Each row:** the target's name (and its range when the Kestrel gave one), the elevation, the wind (and the gust when known). Mil, as the rest of the unit.
- **The current row** is highlighted and kept on screen. Tap: next target; hold: previous, going round the card, as on the band. Recoil can move it when the Dope page's setting is on, as in Match.
- **Behind the rows** the chosen background shows as it does behind the band. With the background Off the screen is black with white rows.
- **No timer.** With DOPE chosen the timer tile is not offered; the dope tile is, for hand entry and the Kestrel pull.
- **The current target moves from the band to the card's own module,** so Match and DOPE step through one position.

## How the unit shows it works

To be written with the mock-ups. At least: snaps of a 5-target and a 12-target card in both rotations over each background; stepping by tap, hold and recoil; the memory checks of ADR-017's `layer_tour` with DOPE in the round.

## Open questions

- How many rows, and how large the digits, in each rotation (mock-ups).
- Whether the rows scroll with the current target or page.

## Not in this ADR

- Range by hand, and any ballistics on the unit.
- Imperial units.
