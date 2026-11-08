# ADR-011 progress

## Step 0: measured on the COM13 unit (2026-11-08)

The unit came with old firmware and a persisted tare. It was flashed (full image) with `main` plus two debug commands: `grav` gives "up" in the sensor's frame from the newest game rotation vector, with the level's raw roll and pitch; `bno tare clear` clears the run-time and the persisted tare.

| Pose | Tare | "Up" in the sensor's frame | Level's raw roll / pitch |
|---|---|---|---|
| Rotation 0 edge | old, persisted | (-0.90, 0.43, 0.02) | 87° / 64° (the level showed 177) |
| Rotation 0 edge | cleared | (-0.042, 0.998, -0.046) | 92.6° / 2.4° (Euler) |
| Rotation 1 edge | cleared | (0.9996, -0.001, -0.028) | |
| Rotation 1 edge, after a reboot | cleared | (0.998, -0.020, 0.065) | |

- The old tare's frame did not match the unit: an upright unit read 64° of pitch.
- The sensor's own frame: gravity along +Y on the rotation 0 edge and along +X on the rotation 1 edge, with Z the screen's normal. The in-plane angle `atan2(x, y)` therefore reads 0 and 90° on those edges, the level's existing rotation rule.
- The cleared tare stays cleared after a reboot, so the persisted record was deleted.
- Corrected: the draft's "turn it 90° on a table" test cannot show the fault. The Euler roll and pitch depend only on gravity's direction in the tared frame; the heading enters once, at tare time.
- Not measured yet: the drift of the accelerometer's dynamic calibration (item 3).

## Steps 1 and 2: built and run by the owner (2026-11-08)

### Built

- **`sensor_frame.c`:** the calibration (stored in NVS namespace `SFR`, separate from the sensor settings), "up" from the game rotation vector quaternion, and cant and pitch from it.
  - The level loop uses them in place of the Euler roll and pitch.
  - At boot it deletes a persisted tare if the sensor has one (`bno085_clear_persisted_tare`, nothing written otherwise).
  - Uncalibrated, "up" is +Y.
- **`sensor_calibration_view.c`:** the routine, on the tile and entry of the old Tare page (Advanced, renamed "Sensor calibration", no confirmation: its first screen has Start and Cancel). The screen turns to rotation 0 for it and back afterwards.
- **Save:** stores the calibration and clears the rifle zero (`digital_level_view_clear_zero`, which replaces the old tare's zero hack).

### Changed with the owner, run by run

The logs are `logs/rc/cal_try*_com13.log`. A cable drop during the turn cut some of them.

1. **First run:** placement A was captured; B never was. The USB cable flexed when the unit was turned around. The owner asked for a gauge showing how far the turn still has to go.
2. **Repeat too large (0.104):** placement A was taken while the reading still crept, by about 0.25° over several seconds after placement as the game rotation vector's fusion settled.
   - The capture became a sliding 2 s window, taken only when its first and last second agree within 0.02°.
   - Also, B was only 162° from A, where the surface's tilt cancels exactly only at 180°. The turn zone was raised to 170° and 10°.
3. **One fail (0.119), one pass (0.051):**
   - The owner saw that a capture could start right after passing through the zone, with the unit still moving.
   - The ring now stays a turn gauge (degrees to go) until the unit is within 5° and its heading has held within 1° for 1 s.
   - Moved away during a capture: it restarts and says "It moved. Put it back to 0°, hands off."
4. **One fail, one pass:**
   - The whole angle between A and C was mostly the unit rocking on its edge, out of the screen's plane (one run: 0.20 in all, 0.006 in cant).
   - The repeat is now judged on the cant only (still 0.1°), and the whole angle is logged.
   - The ring keeps the degrees to go and turns orange while capturing; "Still" and "Settling" were removed (owner).
5. **Two passes (owner).** Saved: offset 1.02°, surface 0.49°, repeat (cant) 0.076°.

The unit's offset across the runs: 1.12, 1.05, 1.03, 0.91, 1.01, 0.93, 1.02°. It repeats within about ±0.1°.

### Left

- **Drift of the accelerometer's dynamic calibration** (step 0, item 3):
  - Read the level on the rotation 0 edge after the save, then after handling, and after a power-off by the PMIC.
  - This decides whether the accelerometer's dynamic calibration is turned off at boot.
- **The second problem unit, and the owner's own unit:**
  - This firmware deletes their persisted tare at boot, so each needs the routine once.
  - Until then the level uses the uncalibrated frame (about 1° off on COM13).
- `state_change` and captures of the routine's screens, before closing.
