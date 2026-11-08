# ADR-010: a third level style, chevrons that point the way to turn

Status: **Open** 2026-11-08. The owner chose the design from mock-ups drawn on the unit's own screen captures: "Let's build it first and try to see if that works." Step 1 built and changed with the owner on the unit (full screen, hazard-tape chevrons, level in full colour); step 2 tried and rejected (tearing visible). Left: the owner's check that the chevrons point the way to turn in both directions. Details in [progress](progress.md).

## What we want

Level styles 1 ("Level") and 2 ("Block") are still pictures that copy existing products (the MDT Send It, the SG Pulse Pro). The third style uses the large AMOLED for something they cannot do: an animated figure that tells the shooter, at a glance, which way to turn the rifle.

## Owner's direction (2026-11-08)

- "Thinking out of the box": figure, colour and motion together, as a traffic arrow or a car's sequential turn signal does.
- Chevrons, not the sequential LED arrow or the rotation arrows ("the other two are not so obvious").
- A black background with coloured chevrons, for the AMOLED. (Styles 1 and 2 keep their full-screen colour.)
- The chevrons point the way the rifle should be turned.
- Their height is half the screen by default.
- Behind all text and numbers, as the other styles are.
- Tied to a later option: anti-tearing off, trading a clean picture for more updates a second.

## What we know

The research and the measurements behind the design, from the discussion of 2026-11-08:

- **Products:** the existing cant indicators signal only level or not level, with three-colour LEDs (MDT Send It Gen2, SG Pulse Pro) or a lamp on one side of the reticle (Sig LevelPlex, Leupold). One patent varies a lamp's flash rate with the amount of cant. None uses motion or shape on a screen.
- **Motion:**
  - Audi's own study reported 34% faster reactions and five times fewer errors with sequential turn signals.
  - An independent study (Flannagan et al., SAE) measured no clear difference in speed, though people preferred them.
  - Off the centre of vision, where the shooter sees the unit while aiming, colour sensitivity falls and slow motion stays visible.
- **Direction:** aviation's attitude displays have a long record of roll reversal errors (4 to 9% in fixed-base simulator studies). The arrow therefore has one meaning only, the way to turn; the colour keeps its meaning in every style, the side that leans.
- **Flashing:**
  - Car turn signals flash 60 to 120 times a minute (FMVSS 108).
  - Accessibility guidance (WCAG 2.3.1) caps flashes at three a second.
  - The chevrons move at most 3 sweeps a second, and nothing flashes the whole screen.
- **Colour blindness:** about 8% of men have red-green colour deficiency. The default colours (light blue, amber, light green) avoid that pair, and the direction is carried by shape and motion too.
- **The screen:**
  - Drawing costs about 197 ns a pixel (ADR-009 step 3).
  - With the anti-tearing wait on, the moving level showed 21 to 26 updates a second.
  - In landscape only about 115 px are free between the timer and the dope readout (215 px in portrait), so chevrons of half the screen run behind them.

## What we will do

### Step 1: the style

`digital_level_view_type_3.c`, chosen as "Chevrons" on the Level page.

As built, after the owner's changes on the unit:

- **Within the level threshold:** the whole view in the levelled colour, as style 1 (the first build's two green bars were not obvious enough). Leaving the level needs 0.1° more than the threshold, so the view does not flicker at the edge.
- **Outside the threshold:**
  - The whole view in chevrons like hazard tape: equal stripe and gap, 45°, a period of 0.6 of the view's height, in the leaning side's colour on black.
  - They point and move the way to turn: the top of the rifle leaning right turns left.
  - Their speed grows with the cant, from 0.3 periods a second at the threshold to 3 at 3° past it.
- **Behind everything:** the angle pill and the dope draw on top, as with the other styles.
- **Drawing:**
  - One period is rendered once per direction into an RGB565 image in PSRAM, anti-aliased by 4 × 4 supersampling. It is redone when the rotation or a colour changes: 138 ms in landscape, 367 ms in portrait.
  - An image widget tiles it, and each frame only shifts its x offset, which LVGL draws as a memcpy.
- **Animation:**
  - An LVGL timer at the panel's rate (16 ms) reads the newest angle itself every frame and moves by the elapsed time.
  - It also refreshes the angle pill, because the level loop's update rarely gets the LVGL lock while the view redraws.
  - While level nothing redraws. While idle the timer is paused with the others (ADR-009).
- **Pitch:** not shown.

Shown on the unit:

- captures in portrait and landscape;
- the owner's eye: the direction is right, the speed reads as the size of the cant, and the level is calm;
- `rc_run.py scenario level_motion`: the frames a second and the drawing's share of the time while moving;
- `state_change` passes.

### Step 2: anti-tearing off (tried, rejected)

The flush can start each write without waiting for the panel's scan. With the band of the time, this raised the chevrons from 22 to 43 frames a second, but the owner saw tearing, so anti-tearing stays on. The switch stays as a RAM-only remote-control command (`tear free|off`) for measurements. 59 frames a second without tearing would need drawing and sending to overlap; that is a redesign of ADR-002's flush, not planned.
