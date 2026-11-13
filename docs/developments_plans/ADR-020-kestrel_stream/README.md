# ADR-020: a Stream foreground, one live target from the Kestrel

Status: **Draft** 2026-11-13. Not approved. Needs ADR-017 (the foreground slot); its layout is best shared with ADR-018.

## What we want

A foreground that shows the Kestrel's present solution as it changes: range a target, and its hold is on the rifle. One target, read from the Kestrel again and again while the foreground is chosen.

## Owner's direction (2026-11-13)

- "Stream (one target, continuous polling)."
- HUD or Device mode, whichever works. The protocol is in `kestrel_5700_connect_library`.
- This reverses one decision of ADR-007 (2026-11-07): "no live view and no continuous link", made because at a PRS match the Kestrel is often switched off after the dope is worked out. Match and its one-off Pull stay as they are; Stream is a separate foreground for when the Kestrel stays on.

## What we know

- **One link serves both names.** The firmware's HUD mode is the Kestrel in Bluetooth, Conct = Device with this unit paired as its HUD: the Kestrel connects to the unit, and reconnects by itself within about a second of the unit advertising (ADR-007).
- **The live read exists and is verified over that link** (the library's ICD, section 7.8 and H-10): `GetRemoteSoln` (`0x00a0`) is answered with the selected gun's name, target range, direction of fire, elevation, wind 1 and wind 2, both wind speeds and the wind direction. Holds are always mil and range always metres. The vendor app polls it about once a second. The firmware's link component already decodes a full solution; the service only pulls the Target Card today.
- **What a live view must survive** (ADR-007, the ICD): the link drops every 2 to 70 s and comes back in about 1.5 s, authenticating again each time; while a value is edited on the Kestrel the solution reads NaN; a disabled gun gives no solution; the first connection can be slow.
- **Memory:** the Bluetooth stack takes 38 KB of internal RAM while it runs (108 KB free with it up, against the 60 KB pass mark), and gives it back to the byte when stopped (ADR-007). So Stream costs internal RAM only while it is the chosen foreground.
- **The radio:** Bluetooth with Wi-Fi connected is a stable mix; with the unit's set-up access point it is not, and a Pull is refused during set-up and updates (ADR-007).
- **Idle:** the unit stops its radio when it idles and sleeps between uses (ADR-009).

## What we propose

- **Stream is a service mode beside the Pull:** while the Stream foreground is chosen and the unit is awake, the Kestrel service keeps the stack up, lets the Kestrel connect, authenticates and polls the solution about once a second, publishing each reading as an event. No LVGL in the service.
- **The foreground shows one target large:** range, elevation, wind and gust, in the layout ADR-018 settles for a single target. It says plainly when the reading is not live: no link, or the Kestrel busy. It never shows an old hold as if it were current.
- **Stream never writes the dope card.**
- **Leaving Stream stops the stack,** and the internal RAM comes back. Idle stops it too; a wake starts it again.
- **Not during set-up or an update,** as for a Pull.

## How the unit shows it works

To be written when the ADR is opened. At least, with the owner's Kestrel: range changes on the Kestrel arrive on the unit; the Kestrel switched off and on; a value edited on the Kestrel; internal RAM before, during and after, and its lowest; an hour's battery use beside Match.

## Open questions

- Where Stream is used, which decides the rest: how long a hold may be shown after the link drops, and whether the unit may idle while streaming.
- Whether Wi-Fi goes off while Stream is chosen, for the radio time and the battery.
- The battery cost of Bluetooth held up, measured before it is promised.
- Phone mode (the unit connecting to the Kestrel) is left out unless the owner wants it: the Kestrel does the reconnecting in HUD mode.

## Not in this ADR

- Changing anything on the Kestrel from the unit.
- More than one target at a time: that is the card, ADR-018.
