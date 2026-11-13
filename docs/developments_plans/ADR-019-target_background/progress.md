# ADR-019 progress

2026-11-13. Two units: COM13 (`owmc-3861330`) on a stable desk, over USB; the turret unit (`owmc-8626470`) on the OpenAutoTurret, over Wi-Fi, updated over the air from v1.4.1-2 to this build (`ota_host.py --force --importance 1`, Install and Restart tapped over the remote control). Logs: `logs/rc/rc_adr019_*` (not tracked).

## Step 1: built

- **The gyro rotation vector in the sensor driver** (`components/bno08x`): `bno085_enable_gyro_rotation_vector_report` and `bno085_get_gyro_rotation_quaternion` (the quaternion, its angular velocity and a count). Its settings in the sensor are the defaults: based on the game rotation vector, no prediction.
- **The `aim` trace** on the remote control: `aim <s> [us]` logs both outputs' quaternions at full resolution 25 times a second for up to 120 s; the gyro rotation vector runs only during the trace (a report every 5000 us unless given). `aim cal <0|1>` switches the gyroscope's self-calibration (RAM only; a reboot turns it on).
- Yaw is taken as the heading of the screen's normal (the bore) about the vertical, pitch as its angle above the horizontal, worked out on the host.
- Build: no warning from our code.

## Step 1: measured

**Tilt, from `grav` on the firmware as it was** (four readings a second for 60 s, 0.1 mil a digit):

| Unit | Scatter (sd) | Largest move inside 10 s | Drift, first half to second half |
|---|---|---|---|
| COM13 on a stable desk | 0.12 and 0.19 mil | 0.5 and 0.7 mil | 0.09 and 0.01 mil |
| Turret unit, quiet axis | 0.22 and 0.27 mil | 0.9 and 1.3 mil | 0.00 and 0.09 mil |
| Turret unit, the axis the pitch motor holds | 0.78 mil | 3.1 mil | 0.01 mil |

**Yaw and pitch, left still** (`aim`, 60 s, COM13 on the stable desk, 30 s after a restart, calibration on):

| Output | Axis | Scatter | Drift | Largest move inside 10 s | Whole minute |
|---|---|---|---|---|---|
| Game rotation vector | yaw | 0.04 mil | +0.02 mil/min | 0.13 mil | 0.13 mil |
| Game rotation vector | pitch | 0.17 mil | +0.42 mil/min | 0.57 mil | 0.73 mil |
| Gyro rotation vector | both | 0.00 | 0.00 | 0.00 | 0.00 |

The gyro rotation vector did not move at all while the unit was still, with 200 reports a second arriving.

**Yaw steps on the turret** (`manual_step yaw` only; each hold 6 s, the unit's reading taken over its last 2 s; mil from the start; the encoder is the turret's own):

| Hold | Encoder | Unit, calibration on | Unit, calibration off |
|---|---|---|---|
| +1 degree | 13.8 / 14.6 | 17.4 | 12.5 |
| back | 3.1 / 2.1 | 9.3 | -0.6 |
| +0.5 degree | 4.6 / 3.8 | 15.8 | 1.2 |
| back | 2.3 / 1.5 | 17.8 | -2.2 |
| +5 degrees | 84.9 / 84.4 | 104.5 | 79.1 |
| back | 1.5 / 0.0 | 163.9, running | -3.3 |
| slow pan out (ten half-degree steps) | 28.4 / 29.9 | 934, running | 29.8 |
| pan back | 6.1 / 6.9 | 1668, running | 13.5 |

(The encoder column gives the two runs. The turret does not reach every step it is given; the encoder is what it did.)

- **Calibration on:** after the 5 degree step and back, the hub's angular velocity read a steady 40 mil a second, mostly about the vertical, with the turret still. Yaw ran at that rate for the rest of the run, 2128 mil in all. Before that it had already gained 3 to 5 mil on the encoder at each step. Left still afterwards, two minutes later: 16 mil a minute.
- **Calibration off:** no runaway. The reading stayed within 2 to 8 mil of the encoder over 90 s; inside a hold it scattered 0.06 to 0.30 mil.
- **The rig's own noise:** while holding, the turret shook the unit in rotation by about 35 mil a second (the median size of the angular velocity), with peaks of 160.
- **Tilt through both runs** stayed within 3.5 mil (0.2 degrees) of the start, runaway included.

## What step 1 found

- The first still run of the day (COM13, 3.3 mil of drift in 30 s) was the desk, not the sensor: gone on a stable desk.
- The datasheet's warning is real on this unit: a slow turn on a steady mount fools the gyroscope's self-calibration (section 3.1.3). A rifle on a bipod, panned between targets, is that case. Whether switching the calibration off is the answer was settled against later the same evening (below).
- The turret's pitch motor makes it a rough rig for yaw. Results on it are a worst case.

## Step 1, later the same evening: the calibration switch on the desk

COM13 on the stable desk, `aim` for 45 to 60 s, each spell with the gyroscope's self-calibration set as shown. The angular velocity is the median size of what the gyro rotation vector reports.

| Spell | Calibration | Yaw drift | Yaw, whole spell | Angular velocity |
|---|---|---|---|---|
| after a fresh start | on (as started) | +0.02 mil/min | 0.13 mil | 0.00 |
| later | switched off | +8.4 mil/min | 9.1 mil | 16 mil/s |
| | on again | +30.4 mil/min | 21.7 mil | 16 mil/s |
| | off | +11.2 mil/min | 8.3 mil | 15 mil/s |
| | on | +42.6 mil/min | 31.2 mil | 15 mil/s |
| | off | +21.6 mil/min | 16.0 mil | 15 mil/s |
| after a reflash and restart | on (as started) | -0.09 mil/min | 0.28 mil | 0.00 |
| | the same setting sent again | -0.02 mil/min | 0.21 mil | 0.00 |

- In the quiet state the hub reports exactly no rotation and holds the heading. After the calibration was switched off once, it never returned to that state, on or off, until the unit restarted.
- Sending the setting the unit already has changes nothing.
- So the turret's result (off is better) and the desk's (off is worse) do not agree, and "off while aiming" was dropped from step 2.

## Step 2: built (and removed again, see the end)

- **`level_target.c`:** the Target background. The NRANZ 500 and 600 yard face seen from 500 yards; the mark and its trail from the game rotation vector, read by an LVGL timer every 20 ms and smoothed (the quaternion's steps are 0.12 mil, 11 px on this face); only the part of the screen the trail leaves and enters is redrawn. Its trail is in PSRAM.
- **The centre, by a hold on the face.** Touches on the level itself (away from the tilt box and the foreground) go to a background that takes them (`touch_callback`). On the Target a tap says "Hold to centre"; a hold says "Let go and hold steady", and one second after the finger has left the centre is taken and it says "Centred". These messages sit at the bottom of the screen, clear of the mark. First built on the tilt box's hold; the owner did not want that (the tilt box is the cant's, and a centre has two axes), and the tilt box has its own job back.
- **The Level page:** Background lists Level, Block, Chevrons, Target, Off (stored 0, 1, 2, 4, 3). The threshold and colour rows show for the three level styles only.
- `scenario layer_tour` walks the five backgrounds.
- COM13 runs this build. The turret unit runs the one before the centre moved to the face (two more updates over the air): its Target still centres on the tilt box's hold.
- Build: no warning from our code; `dependencies.lock` unchanged. Static internal RAM is 80 bytes larger than ADR-018's (the Target's record and few statics, and the aim trace's).

## Step 2: measured

- **The face's size:** on a portrait snap the inner, magpie and outer rings lie at 58.0, 90.3 and 130.3 px from the centre; their sizes in mil give 60.0, 91.5 and 132.0 to the rings' outer edges (the lines are 2 px wide).
- **The centre:** a tap on the face shows "Hold to centre"; after a hold, "Hold steady" right after the release and "Centred" 1.2 s later, the mark in the middle and in view throughout. A tap on the tilt box shows "Hold to set zero", as with every background.
- **Direction, on the turret:** a yaw step that turns the unit left sends the mark to the left edge.
- **On the turret the mark does not stay:** between half-degree steps there and back it wandered by more than the face's width within about 20 s. The turret shakes the unit; this is the worst case, not a rifle.
- **Memory:** `scenario layer_tour --min-largest-dma 70000` passes, largest DMA block 81,920 throughout; on the final build free internal RAM 161,831 at the start and at the end. (Two earlier runs read 162,591 at the start and 161,815 at the end, inside the scenario's 1 KB allowance; a direct probe of Target on and off read 161,819 before, during and after. Not a leak.)
- **With a foreground:** Match and Dope lie across the face. For use with the foreground Off for now.

## Step 2: the owner's try (2026-11-13)

"It will drift even if stable." Measured at once on COM13 as it sat, untouched, in its normal state (no calibration switch used since its restart): yaw +27.4 mil a minute, a steady slide (0.47 mil of scatter about the line), 13.6 mil in 30 s; pitch +0.5 mil a minute. The angular velocity read 6.3 mil a second (median), not the 0.00 of the quiet runs: the hub was not in the still state in which it holds the heading.

- The quiet runs of step 1 (0.13 to 0.28 mil in a minute) were the special case: a unit just restarted and left alone on a quiet desk. A unit that has been handled, on an ordinary surface, slides at about 0.45 mil a second: across the whole face in about 6 s.
- So the mark as built, an absolute place since the last centring, does not serve in windage. Elevation does hold.

## Stopped (2026-11-13)

The owner asked whether to go on or discard the idea, and on the numbers above decided to stop ("Yes, close it").

- Removed: `level_target.c` and its header, the Target's value among the backgrounds and its row on the Level page, the touch routing from the level to a background, the low hint. The level view's files are as they were before this ADR.
- Kept: the gyro rotation vector in the sensor driver, the `aim` trace on the remote control (in `AGENTS.md`), this record.
- A unit that had saved the Target as its background shows the default background: an unknown stored value falls back (ADR-017).
- Not tried, and not needed any more: muzzle up against the mark, a slow turn and back on a rifle, dry-fire recordings.
- Both units run the build without the Target (COM13 flashed over USB, the turret unit updated over the air): `layer bg 4` is refused on both, the `aim` trace runs, and on COM13 `scenario layer_tour --min-largest-dma 70000` passes with free internal RAM 161,987 at the start and at the end. Build: no warning from our code.
