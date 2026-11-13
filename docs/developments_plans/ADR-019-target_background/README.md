# ADR-019: a Target background, the point of aim on a real-size target

Status: **Draft** 2026-11-13. Not approved. Needs ADR-017 (the background slot). The design waits on this ADR's first step, a measurement.

## What we want

A background for training: a target face, and a mark that shows where the rifle points on it, so a shooter sees their hold against the rings they will shoot at.

## Owner's direction (2026-11-13)

- The bull's-eye view (`point_of_aim_view.c`, disabled since before ADR-012) comes back as a background.
- F-class and target rifle targets, by the NZ rules.
- The face mimics the real target's size, roughly, at a fixed distance (300 yd, 500 yd and so on).
- If the existing code has the target wrong, fix it.

## What we know

### The NZ targets (NRANZ Rules, 2021 edition of June 2023, Annex T/D)

Diameters in mm. A target's size as an angle is what the unit must show; 1 mil is 1 mm at 1 m.

| Distance | Aiming mark | V-bull | Bull | Inner | Magpie | Outer |
|---|---|---|---|---|---|---|
| 300 yd | 560 | 65 | 130 | 260 | 390 | 560 |
| 500 and 600 yd (NZ: one target) | 915 | 145 | 290 | 600 | 915 | 1320 |
| 700 m to 1000 yd (NZNRA match target) | 1120 | 255 | 510 | 1120 | 1830 | rest of target |

- F-class shares the target. Where both shoot on one, a "supervee" ring of half the V-bull's diameter counts V for F-class (D3.3).
- Frames: 1.2 m square at 300 yd, 1.8 m square at 500 to 700 yd, 1.8 x 2.4 m at long range.

As angles:

| Distance | Aiming mark | Bull | V-bull | Supervee |
|---|---|---|---|---|
| 300 yd (274 m) | 2.04 mil | 0.47 mil | 0.24 mil | 0.12 mil |
| 500 yd (457 m) | 2.00 mil | 0.63 mil | 0.32 mil | 0.16 mil |
| 600 yd (549 m) | 1.67 mil | 0.53 mil | 0.26 mil | 0.13 mil |
| 800 yd (732 m) | 1.53 mil | 0.70 mil | 0.35 mil | 0.17 mil |
| 900 yd (823 m) | 1.36 mil | 0.62 mil | 0.31 mil | 0.15 mil |
| 1000 yd (914 m) | 1.22 mil | 0.56 mil | 0.28 mil | 0.14 mil |

### The existing code

- **The face** is a 44 inch black disc with rings at 32, 20 and 10 inches. That is the long-range target of the strict ICFRA rules (inner 814 mm, magpie 1120 mm). The NZNRA match target has no 32 inch ring: its inner is the 44 inch aiming mark and its magpie is 72 inches. The short-range faces are not in the code.
- **The scale** is set by a target distance of 10 m, about 90 times closer than 1000 yd: the face covers 112 mil instead of 1.2.
- **The mark** is a chart of the last 25 points of yaw and pitch from the game rotation vector, zeroed by a tap on the chart.
- **It cannot run as it is.** It has its own task, reads the sensor itself beside the level's reader, and switches the game rotation report with the tile, which ADR-014 forbids (the fusion runs whenever the unit is on).

### The sensor against these sizes (COM13 lying still on the level tile, 2026-11-13, 600 readings in 30 s)

| Reading | Drift over 30 s | Largest move inside 0.5 s |
|---|---|---|
| Cant | 3.3 mil | 0.5 mil |
| Pitch | 1.75 mil | 0.35 mil |

- The `trace` command prints 0.01 degrees, which is 0.17 mil: too coarse to see the short moves properly.
- This agrees with ADR-014: the fusion wanders 0.15 to 0.26 degrees over 30 s while still.
- **Yaw is not measured.** The game rotation vector has no compass: nothing corrects yaw, so it should drift more than pitch.
- So at real size the mark of a rifle held perfectly still would leave the aiming mark by itself within about half a minute, and the bull is about the size of the reading's own short moves. One desk run is not a verdict; it is the reason for step 1.

## What we propose

1. **Measure first, on the turret,** with a trace that logs pitch and yaw at the sensor's full resolution: the unit still for 60 s; then turret steps in pitch and in yaw. Three numbers decide the design: the noise within 1 s, the drift over 10 s (one aimed shot), and the drift over 60 s.
2. **The verdict decides which of these is built:**
   - **Real size holds for a shot** (drift over 10 s well inside the bull): the real face at the chosen distance, re-centred by holding the tilt box before a shot.
   - **Real size holds only for the wobble** (noise small, drift not): the real face, with the mark shown against its own slow average. It shows whether the hold stays inside the bull, not where on the target the rifle points.
   - **Neither:** the real ring proportions, enlarged, with the scale shown on screen. The owner decides whether that is worth building.
3. **The face** by the NZ rules, chosen by distance (300, 500, 600, 800, 900, 1000 yd), with the supervee ring for F-class.
4. **Drawn as a background** fed by the level's one reader: no task of its own, nothing switched on the sensor.
5. **The zero** is the tilt box's hold, as for the level, not a tap on the target (a tap there belongs to the foreground).

## How the unit shows it works

Step 1's numbers in `progress.md`, then to be written for the design that follows.

## Open questions

- Which distances, and whether metres are needed beside yards.
- What the foreground may cover: the face needs the middle of the screen.

## Not in this ADR

- A compass (the magnetometer) for yaw: a rifle is steel.
- Scoring, or recording shots.
