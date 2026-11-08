# ADR-011: factory calibration of the sensor, as a guided routine

Status: **Built** 2026-11-08, merged as PR #8 and released in v1.1.0. Approved by the owner ("Approve and flash to give it a try first"). Step 0 measured, steps 1 and 2 built and run by the owner on the COM13 unit ("Two passes, looks good"). With a placement marker, the accelerometer's dynamic calibration moved the zero by about 0.05 to 0.1° per handling (most of an earlier 0.25° was placement); it is off (gyroscope only), which held it within 0.02°. `state_change` passes. All three units are calibrated (owner, 2026-11-08). Details in [progress](progress.md).

## What we want

The Tare is the unit's factory calibration: it maps the BNO085's frame onto the OWMC's. Today it needs the unit pointed at true north before Tare is pressed, which is fragile, and two units cannot be tared reliably: left and right tilt do not come out right. We want a calibration that gives the same result every time, needs no compass and no level table, and guides the person through it step by step.

## Owner's direction (2026-11-08)

- The Tare is factory calibration, mapping the motion of the OWMC in its space. A better approach than tare is welcome; heading north and tapping Tare is fragile.
- The reference is not the rifle mount. The unit is calibrated on a flat surface, and the edge it stands on maps to screen rotation 0.
- A complete calibration routine with a wizard, entered where the Tare is today (Settings, Advanced, "Tare calibration").

## What we know

From the code, the BNO085 datasheet (1000-3927 v1.17) and the SH-2 Reference Manual (1000-3625 v1.9):

- **What Tare does today** (`sensor_calibration_view.c`):
  - It sends Tare Now on all three axes with the magnetic rotation vector as the basis, then Persist Tare.
  - All-axis tare "solves for tilt and heading" to align the outputs with East, North, Up (datasheet 4.1.1), and with that basis it reorients all motion outputs (SH-2 6.4.3.1).
  - The level then reads the Z-Y-X Euler roll of the game rotation vector. After the tare that roll is a rotation about whatever direction was East at tare time, so the result depends on the compass heading during the tare. It is cant pointing east, pitch pointing north, a mix in between.
  - The heading comes from the magnetometer, which the datasheet says must have resolved north before a tare. Next to a rifle or indoors it rarely has.
- **The persisted tare and the level's report do not match:**
  - Persist Tare stores the result in the master sensor orientation record (FRS 0x2D3E) and "only persists the tare applied to rotation vector and geomagnetic rotation vector" (SH-2 6.4.3.2).
  - The datasheet adds "Persist tare does not apply to the Game Rotation Vector", which is the report the level reads.
  - A persisted tare is cleared only by deleting that record (SH-2 6.4.3.3). The firmware never clears it.
- **Accelerometer calibration:**
  - The BNO085 calibrates the accelerometer's zero-g offset dynamically by default (datasheet 3.1). Offset errors "manifest as tilts" (3.1.2).
  - The calibration data is kept in RAM and written to the sensor's flash only at a reset that is not a power-up (3.4). The PMIC switches the unit off, so a power-up may start from older data.
  - The settings of which sensors calibrate do not persist across resets (3.1.1).
- **Accuracy:** the game rotation vector's tilt error is 1.5° static and 2.5° dynamic, nominal (datasheet 6.7). That is more than the level's 1.0° threshold, so each unit needs its own calibration, and the calibration must stay put.
- **The geometry:** the BNO085 sits flat on the board, opposite the screen, so its Z axis is the screen's normal by design. On the rotation 0 edge, cant is a rotation about that normal (a wall level).

## What we will do

### Step 0: measure first (on one of the two problem units)

1. **The fault:**
   - The Euler roll and pitch depend only on gravity's direction in the (tared) frame, so turning the unit about the vertical after a tare does not change them. The heading enters once, when the tare is taken, which is why it must be taken facing north.
   - Shown instead by reading the gravity direction in the tared frame with the unit standing on an edge: it should lie along one tared axis, and does not if the tare was taken in another pose or heading.
   - (Corrected 2026-11-08: the draft proposed a 90° turn on a table, which cannot show it.)
2. **The sensor's own frame:**
   - Clear the tare (run-time and persisted), then read the gravity direction on the rotation 0 edge and after a 90° turn about the screen normal.
   - This confirms that the sensor's Z is the screen normal, and the signs.
3. **Drift of the accelerometer's calibration:**
   - The same pose read before and after handling the unit in several orientations, and before and after a power-off by the PMIC.
   - This decides whether step 1 turns the accelerometer's dynamic calibration off.

### Step 0 results (2026-11-08, COM13 unit, current firmware plus `grav` and `bno tare clear`)

- **The old tare:** the unit stood on its rotation 0 edge (saved rotation 3). In the tared frame "up" was (-0.90, 0.43, 0.02), the Euler roll 87° and pitch 64°, and the level showed 177. The tared frame does not match the unit: an upright unit read 64° of pitch.
- **Tare cleared** (`bno tare clear`: run-time reorientation zero, master sensor orientation record deleted). On the rotation 0 edge "up" in the sensor's own frame is (-0.042, 0.998, -0.046):
  - Gravity runs along the sensor's +Y axis and lies in the X-Y plane, so the sensor's Z is the screen's normal, as assumed.
  - What is left is about 2.4° in the screen's plane and 2.6° out of it: the unit's own offset plus the bench's tilt, which the calibration's turn-around separates.
- **Still to read:** the rotation 1 edge (the direction angles count), and the drift (item 3).

### Step 1: the measurement, independent of heading

- **Gravity:** from each game rotation vector report, the gravity direction in the sensor's frame (the third row of the quaternion's rotation matrix). The game rotation vector's tilt is referenced to gravity; its heading is never used.
- **Calibration:** the "up" direction `r` (a unit vector in the sensor's frame) with the unit standing on its rotation 0 edge, the surface's tilt removed (step 2).
- **Bore axis `b`:** the sensor's Z axis, made exactly perpendicular to `r`.
- **Cant:** the angle from `r` to the gravity direction, projected onto the plane perpendicular to `b`, measured about `b`.
  - It replaces the Euler roll as the level's raw roll. The level's rotation term (rotation × 90°) and its zero stay as they are.
- **Pitch:** the gravity direction's component along `b`.
- **Stored in our NVS:** `r`, the date, and the two quality figures (surface tilt and repeat). This is visible on the page and can be cleared. Nothing is written into the BNO085.
- **Old tares are cleared:**
  - At the routine's start: Set Reorientation to zero (run-time) and the master sensor orientation record deleted (FRS 0x2D3E), then a sensor reset.
  - Units with an old tare and no new calibration fall back to the nominal `r` (the sensor's design axes) and show "Not calibrated" on the page.
- **Accelerometer:** if step 0 shows drift, the BNO085's dynamic calibration is set to the gyroscope only at every boot (`sh2_setCalConfig`). The gyroscope's calibration stays on: it matters while moving, not for the tilt at rest.

### Step 2: the routine (wizard)

**Placements:** two, on the rotation 0 edge, to remove the surface's tilt.

- Each reading is the mounting offset plus the surface's tilt. Turning the unit 180° about the vertical, on the same edge and the same spot, flips the sign of the surface's tilt in the unit's frame.
- The average of the two is the unit's own offset; half their difference is the surface's tilt. So the surface need not be level, only flat and firm.
- A third placement, back in the first pose, checks repeatability.

**Captures:** each capture waits until the unit is still.

- Sampling: about 2 s of reports at 50 Hz, and every sample within 0.1° of their mean; otherwise it starts again.
- Turned around: the routine detects it from the game rotation vector's heading changing by about 180°, then waits until the unit is still.
- Audible: the routine beeps once when a capture is taken and twice when it is time to move. The buzzer tells the person what to do while the screen faces away.

**The screens:**

- **Layout:**
  - The screen turns to rotation 0 for the routine, because the unit stands on that edge. The owner's rotation comes back afterwards.
  - Black, white text, the orange primary button, 122 px targets: the project's design language.
- **The sequence:**
  1. **Start.** "Sensor calibration. About a minute. You need a flat, firm surface." It shows the current state ("Calibrated 2026-11-08", or "Not calibrated"). Start / Cancel.
  2. **Placement A.** "Stand the unit on this edge, screen towards you. Hands off." The bottom edge glows orange to show which edge. A ring fills while the unit is still and restarts if it moves. One beep on capture.
  3. **Turn it around.** "Turn it around: same edge down, same spot. Two beeps when done."
  4. **Turn it back.** "Turn it back to face you." It captures as in placement A.
  5. **Result.**
     - "Pass": the unit's offset, the surface's tilt, and the repeat figure (the angle between placements A and C). Pass when the repeat is within 0.1° (the owner may change it).
     - Save / Redo.
     - Saving also clears the rifle zero (the angle pill's hold), because it was taken against the old calibration. The screen says so.
     - "Repeat too large" offers only Redo.
- **Cancel:** at any step it leaves the old calibration unchanged.

**Shown on the unit:**

- On a problem unit, three calibrations agree within 0.1°.
- A calibration taken facing another compass direction gives the same result within 0.1°.
- The four edges read 0, 90, 180 and 270° at the matching rotations.
- `state_change` passes.
- The screens are captured in rotation 0.

### Not in this ADR

The rifle zero (holding the angle pill) also takes a single reading as the finger lifts. It could use the same capture: hands off, wait for stillness, average. That is a separate change, if wanted.

## Open for the owner

- Approve the approach (no BNO085 tare; gravity, two placements and a check).
- The pass level for the repeat (0.1° proposed).
- Which units to run step 0 on, and when. The sample unit is on COM13 with old firmware, which has no remote control. Step 0 needs it flashed with current `main` plus a debug command that prints the gravity direction. Flashing resets it and may reset settings whose stored layout has changed since; the owner decides.
