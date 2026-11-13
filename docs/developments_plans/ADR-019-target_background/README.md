# ADR-019: a Target background, the point of aim on a real-size target

Status: **Stopped** 2026-11-13, by the owner's decision after trying it. Steps 1 (the measurement) and 2 (the simplest Target) were built and measured ([progress](progress.md)); the sensor's own noise and drift are as large as the rings a real-size target would be drawn with. The Target view was removed; the record and the measuring tools stay.

## What we want

A background for training: a target face in its real angular size, and a mark that shows where the rifle points on it, so a shooter sees their hold against the rings they will shoot at.

## Owner's direction (2026-11-13)

- The bull's-eye view (`point_of_aim_view.c`, disabled since before ADR-012) comes back as a background.
- F-class and target rifle targets, by the NZ rules.
- The face mimics the real target's size, roughly, at a fixed distance (300 yd, 500 yd and so on).
- If the existing code has the target wrong, fix it.
- The old view used the rotation vector. Do not stop there: use the research, for example how a VR headset tracks where it points, and the full potential of the BNO085.
- For measuring: the Wi-Fi unit on the turret (`owmc-8626470`, 192.168.2.165) and the turret at `rpi-turret`. The turret's pitch motor is very noisy.
- Dry fire.
- One fixed face, at 500 or 300 yards: the targets are much alike in mil at every distance.

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

### How a headset knows where it points (research, 2026-11-13)

- **Three sources, each for what it can see.** A VR headset integrates its gyroscope at about 1000 times a second for a smooth, fast orientation. That drifts, so it is corrected: tilt (pitch and roll) from gravity, read from the accelerometer only while the head is nearly still; yaw from a magnetometer, after correcting for the iron in the headset, or from cameras ([LaValle and others, "Head Tracking for the Oculus Rift"](https://typeset.io/pdf/head-tracking-for-the-oculus-rift-463noprt22.pdf); [Oculus, "Sensor Fusion: Keeping It Simple"](https://developers.meta.com/horizon/blog/sensor-fusion-keeping-it-simple/)).
- **Yaw has no reference inside the sensor.** Gravity says nothing about heading. Headsets with no magnetometer or camera let yaw drift and give the user a re-centre; the ones that hold yaw for good do it with cameras or base stations outside the IMU ([VR/AR wiki, sensor fusion](https://vrarwiki.com/wiki/Sensor_fusion)).
- **Position always needs an outside reference** (cameras, base stations). A target at 300 yd or more does not need it: moving the rifle 10 cm sideways moves the point of aim 10 cm, 0.1 mil at 1000 m. Only the rifle's angle matters.
- **Corrections are hidden.** A correction that makes the picture jump while the head is still is disturbing, so it is applied while the head moves.
- **IMU-only shooting trainers do not show an absolute point of aim.** MantisX (gyroscope and accelerometer on the gun, 1000 samples a second) shows the muzzle's movement around each shot: the hold, the trigger press, the break. Live shots are found by recoil; dry-fire shots by their click, with 65 to 95% found depending on the firearm ([Recoil](https://www.recoilweb.com/preview-mantisx-88211.html?nonamp=1), [Guns and Ammo](https://www.gunsandammo.com/editorial/improve-shooting-fundamentals-mantisx-10-elite/389318), [tacticalhyve](https://tacticalhyve.com/mantis-x10-elite-review/)).

### What the BNO085 has for this (datasheet v1.17, SH-2 reference manual)

Used today: the game rotation vector at 50 a second, linear acceleration, the stability detector. Gyro calibration is on; accelerometer and magnetometer calibration are off (ADR-011).

| Feature | What it gives | For the target |
|---|---|---|
| Game rotation vector | Gyro and accelerometer, no magnetometer. Heading drift 0.5 degrees a minute while moving (8.7 mil a minute) | The base: smooth, drifts in yaw |
| AR/VR-stabilised game rotation vector | The same, with corrections applied only while the device moves, so the output never jumps while still | Made for a mark on a display |
| Gyro rotation vector | Up to 1000 a second, low delay, built for head tracking; based on either rotation vector | The trigger press and the break are fast: 50 a second misses them |
| Rotation vector (with magnetometer) | An absolute heading. Error 2 degrees still and 3.5 moving, "in practice 5"; the datasheet advises against it for smooth tracking (its calibration makes jumps) | 2 degrees is 35 mil, seventeen aiming marks. Not for the mark |
| Gyro calibration | Converges when the device is very still; slow turns about the vertical can fool it, and without hand tremor the datasheet advises switching it off | A rifle on a bipod is still, and pans slowly: the case it warns about |
| Stability classifier | On table, stable (held still), motion | Says when a hold begins |
| Tap detector | Single and double taps | A candidate for finding a dry-fire click; untried |

### Measured: tilt, the half that gravity corrects

The gravity vector from the remote control's `grav`, four times a second for 60 s, the level tile shown. One digit is 0.1 mil. Each unit left untouched.

| Unit | Scatter (sd) | Largest move inside 10 s | Drift, first half to second half |
|---|---|---|---|
| COM13 on a stable desk | 0.12 and 0.19 mil | 0.5 and 0.7 mil | 0.09 and 0.01 mil |
| Turret unit, quiet axis | 0.22 and 0.27 mil | 0.9 and 1.3 mil | 0.00 and 0.09 mil |
| Turret unit, the axis the pitch motor holds | 0.78 mil | 3.1 mil | 0.01 mil |

- **Tilt does not drift.** Its noise is about 0.15 mil on a quiet mount: half a V-bull, a quarter of a bull.
- **The turret's pitch motor shakes the unit** by about 0.8 mil while holding. Still tests belong on the desk, or need the motor quiet.
- **An earlier run was wrong.** On 2026-11-13 afternoon COM13 showed 3.3 mil of cant drift in 30 s; the desk was not stable (the owner moved the unit to a stable one, and the drift was gone). Read with the old `trace`, which prints 0.17 mil a digit.

### Measured: yaw (step 1, 2026-11-13, [progress](progress.md))

Read with the `aim` trace built for this ADR: both orientation outputs as quaternions at full resolution, 25 times a second.

- **Left still on a quiet desk, yaw holds.** COM13, in three runs after a fresh start: 0.13 to 0.28 mil of movement in 45 to 60 s. The hub sees that it is still, reports no rotation at all, and holds the heading.
- **Once the unit is not that still, yaw wanders 10 to 40 mil a minute.** On the turret, whose pitch motor shakes the unit, 16 mil a minute; on the desk in the disturbed state below, 8 to 42. The datasheet's figure for a moving device is 8.7 mil a minute.
- **A slow turn on a steady mount fools the gyro's self-calibration,** as the datasheet warns. On the turret, after a 5 degree step and back, the hub reported about 40 mil a second of rotation with the turret standing still, for about a minute, and yaw ran away by more than 2000 mil.
- **Switching that calibration off is not the cure.** On the turret it stopped the runaway: the same steps and a slow pan read within 2 to 8 mil of the turret's encoder over 90 s. But on the quiet desk it made things worse: 8 to 22 mil a minute of steady drift, the hub no longer holding still, and the gyro rotation vector jumping by tens of mil. Switching it on again did not bring the quiet state back; a restart did.
- **The level's own reading is not at risk:** through the runaway, tilt stayed within 3.5 mil (0.2 degrees) of where it began.
- **The gyro rotation vector** arrives 200 times a second as asked and agrees with the game rotation vector in yaw.

### Measured: the Target in the owner's hands (step 2, 2026-11-13)

The simplest Target was built and put on the unit: the 500 yard face at real size, a mark with a short trail from the game rotation vector, centred by a hold on the face. The owner: "it will drift even if stable." Measured at once on COM13 as it sat, untouched, in its normal state:

| Axis | Slide | Scatter left after removing the slide | Largest move inside 2 s |
|---|---|---|---|
| Windage (yaw) | 0.45 mil a second (27 mil a minute) | 0.47 mil | 1.7 mil |
| Elevation (pitch) | none (0.5 mil a minute) | 0.25 mil | 1.0 mil |

The quiet runs above were the special case: a unit just restarted and left alone on a quiet desk, where the hub decides it is still and freezes the heading. A unit that has been handled, on an ordinary surface, is not in that state.

## Why it was stopped (owner, 2026-11-13)

- **The sensor's own movement is as large as the rings.** At 500 yards the bull is 0.63 mil across and the V-bull 0.32. Windage slides across the whole face in about 6 s, and with the slide taken out it still scatters by about a ring; elevation moves up to 1 mil inside 2 s. A good prone hold is 0.1 to 0.3 mil: the unit could show a bad hold, and could not tell a good one from its own noise.
- **It would show false information.** A mark on a real-size target looks exact, and most of what it showed would be the sensor. No information is better than misleading information.
- **Nothing in the unit can supply what is missing,** a reference for heading: the magnetometer is 20 times too coarse, and there is no camera. What was left (the firmware learning the slide, finding a dry-fire click) was research with no sure end, each part needing sessions on a rifle.

## What stays in the tree

- **This record,** so the idea is not tried again without a better sensor: the NZ target sizes, the research, and the measurements ([progress](progress.md)).
- **The `aim` trace** on the remote control and **the gyro rotation vector in the sensor driver** (`bno085_enable_gyro_rotation_vector_report`, `bno085_get_gyro_rotation_quaternion`): the instruments the measurements were made with.

The Target view itself (`level_target.c`), its place on the Level page and its touch handling were removed. The old `point_of_aim_view.c` stays disabled, as before.

## What else was learnt

- **The gyro's self-calibration.** A slow turn on a steady mount fools it (the turret run), and switching it off leaves the hub in a worse state until a restart (the desk runs). It is left on, as it was. The level's reading stayed within 0.2 degrees through the worst of it.
- **A unit left "still" is only as still as what it sits on.** The first drift figure of the day was the desk.
- **The turret's pitch motor shakes the unit;** it is no rig for small angles.

## If it is taken up again

- With this sensor, only a coarse picture could be honest: the wobble of a positional hold against rings of 1, 2 and 5 mil, several times the sensor's noise. That is a different idea from a target at real size, and is not planned.
- A target at real size needs a heading reference outside the IMU.
