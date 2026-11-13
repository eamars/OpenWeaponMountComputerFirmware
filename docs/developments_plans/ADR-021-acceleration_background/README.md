# ADR-021: an Acceleration background, the recoil scope

Status: **Draft** 2026-11-13. Not approved. Needs ADR-017 (the background slot).

## What we want

The acceleration view back, as a background: a trace of what the sensor felt around a shot, so a shooter can see whether the unit detects their rifle's recoil and where to set the trigger level.

## Owner's direction (2026-11-13)

- The acceleration view (`acceleration_analysis_view.c`, disabled since before ADR-012) is one of the backgrounds.
- From the field (2026-11-07): recoil detection is unreliable with .22LR and heavy rifles; never the main trigger for anything. Measured with this view: a 6.5 Creedmoor of about 8 kg with a brake or suppressor stays under 5 m/s², a .22LR under 1 m/s².

## What we know

- **What the view does:** it keeps the last 100 readings of the linear acceleration along the bore (20 a second, so 5 s), and when a reading crosses the trigger level it draws them as a chart with the crossing in the middle, with the trigger level, the reading and the peak as text.
- **It cannot run as it is.** It has its own task and reads the linear acceleration report itself, beside the level's reader, which reads the same report for the recoil trigger. It switches the report with the tile. It was built as a tile on LVGL's light theme.
- **The level's reader already has every reading** and already decides "recoil" from the same trigger level (`digital_level_view_controller.c`).
- **The report runs only when the Advanced setting for it is on.** With it off there is nothing to show.
- A background draws behind the tilt box and the foreground's text.

## What we propose

- **A background fed by the level's one reader:** the reader hands each linear acceleration reading to the chosen background. No task of its own, no report switched; its buffer lives only while it is chosen.
- **The same trigger for the picture and for the unit:** the chart is drawn on the crossing the reader itself acts on, so what the shooter sees is what starts the timer and moves the dope.
- **Black, in the design language of the other views:** the trace, a line at the trigger level, and the peak as a number. No cant colour: the tilt box still gives the angle.
- **With the report off** it says so, and where to turn it on.

## How the unit shows it works

To be written when the ADR is opened. At least: a tap on the bench gives a trace with the crossing in the middle and the peak that the log shows; snaps in both rotations; the memory checks of ADR-017's `layer_tour` with Acceleration in the round.

## Open questions

- Whether a foreground makes sense over it, or it is shown with the foreground Off only.
- Whether it belongs among the backgrounds a shooter picks, or with the trigger level on the Advanced page: it is a tool for setting up, not a view to shoot with.

## Not in this ADR

- A better recoil detector.
