# ADR-014: the level reads right the moment the unit wakes

Status: **Draft** 2026-11-09, waiting for the owner's decision below.

## What we want

When the shooter wakes the unit (lifts the rifle, touches the screen, presses the button), the level shows the rifle's real cant at once, not an angle that is wrong and creeps to the right one.

## Owner's direction (2026-11-09)

- "After sleep, it takes long time to recover." The unit was on battery.
- Debug it over Wi-Fi, on the unit mounted on the turret. To reproduce: the BNO085 asleep during light sleep, tilt, then wake.
- When the screen is off, Wi-Fi disconnects. Perhaps testing on the real unit needs exceptions.

## What we know (measured 2026-11-09 on the turret, on battery)

The unit (`owmc-8626470`) is on the turret's pitch cradle, so turret pitch is the unit's cant. Its cant was followed over the Wi-Fi remote control.
- **Instrument, no exception to the unit's behaviour:** a debug command, `trace <s>` (RAM only), logs the level's cant and the count of game rotation reports every 50 ms for s seconds from the next wake. The unit keeps its log through the idle, and the host reads it once Wi-Fi is back. The radio stays off during idle as in real use.
  - The owner suggested keeping Wi-Fi on during idle for testing. It isn't needed, and it would change what is measured: Wi-Fi restarts at the same moment as the sensor.
- **Awake, a 5° tilt is followed:** the cant moves with the turret (the step takes about 1.2 s), then creeps a further 0.25° over 7 s. It settles 0.4° short of the encoder's 4.9°, both ways. That offset is the mount or the turret, not the wake.
- **After a wake the reading is wrong, and recovers over seconds.** Reports arrive at once: nothing is frozen.

| | Before sleep | 50 ms after the wake | 1 s | 2 s | 4 s | 10 s |
|---|---|---|---|---|---|---|
| Tilted 5° during sleep (settles at −91.3°) | −95.8 | −90.09 (1.2° off) | 0.9° off | 0.3° off | 0.12° off | settled |
| Not tilted (settles at −91.2°) | −91.3 | −90.50 (0.7° off) | 0.6° off | — | 0.2° off (3 s) | settled |

- **The cause is the BNO085's fusion restarting.** While the hub sleeps, only the stability detector runs (ADR-009, for power). On wake the game rotation vector starts again, and its first orientation is up to about 1° off. It converges at the fusion's slow tilt-correction rate. The same happens with no movement at all, so the tilt only adds to it.
  - ADR-009 measured when the first report came after a wake (99 ms), not whether its value was right. This was invisible to it.
- **Proof:** with the game rotation vector kept running while the hub sleeps (`bno alwayson grv 1`, RAM only), the error is gone.
  - Not tilted: −91.17° before sleep, −91.21° at 0 ms and −91.20° at 50 ms after the wake.
  - Tilted 5° during a 30 s sleep: −95.75° at 0 ms after the wake, the right value. The tilt was tracked during sleep.
  - The unit still slept: light sleep through the idle, and no extra wakes once set. One idle right after flipping the switch ended early on a BNO085 interrupt, not seen again.
- **The cost:** fusion running at 100 Hz takes 11 mA by the datasheet (DS p.51), against 0.13 mA for the stability detector alone. ADR-009 estimated the whole idle at around a milliamp, so this would be most of the idle draw. There's no meter here, so this is the datasheet figure, not a measurement.
  - With the defaults (idle after 5 min, power off after 5 h of idle), one idle stretch costs at most about 55 mAh: 6% of the 950 mAh T9 cell. With "Sleep timeout: Never", a forgotten unit would run flat in about 3 to 4 days instead of weeks.
- **A slow tilt does not wake the unit:** a 5° turret step (about 1.2 s) did not trip the stability detector (no BNO085 wake in the counters). A lifted rifle moves harder than that. How hard a movement wakes the unit is a separate question, not measured here.
- **Not explained:** one idle on the turret ended 4 to 5 s after a tilt, by a touch wake (`touch=1`), with nobody touching the unit as far as the run knows.

## The owner's questions, and the experiments (2026-11-09)

The owner asked for the options before the safe path: (1) does a slower report rate cut the cost, (2) force a fresh measurement after a known wake, (3) try path B; A only if the others fail or cost too much. And: does yaw wake the unit?

Experiment build, every switch RAM only and set over Wi-Fi: `bno accel <ms>` (the calibrated accelerometer report), `bno wake fresh|resume`, `bno levelfix on|off` (path B), and `trace` logging the level's cant, the fusion's, the accelerometer's, the correction and |g|.

1. **The report rate barely changes the cost** (DS p.51). The fusion costs 7.50 mA on the VDD rail at every listed rate, which is the gyroscope; the VDDIO part is 3.50 / 5.34 / 6.90 mA at 100 / 200 / 400 Hz. Below 100 Hz only the 3.5 mA part can shrink, so option A at 2 to 5 Hz still costs at least about 8 mA. The accelerometer at 125 Hz costs 1.4 mA, and the stability detector that runs in sleep today 0.13 mA.
2. **A fresh fusion start does not help.** With the game rotation vector stopped before sleep and enabled again after the wake, the first reading was still 0.64° off (−91.77° against a settled −91.13°), converging at the same pace. The error comes from how the fusion starts, whether it resumes or restarts.
3. **Path B works in principle.**
   - **The accelerometer is right at the wake:** −95.46 to −95.71° in the first samples, against a settled −95.65°. It is noisy, about ±0.25° a sample on the turret, and up to about ±1° in one run (|g| between 9.5 and 10.3 m/s²): the turret's yaw motor shakes it (owner).
   - **Once settled, the two agree** (−95.62° against −95.64°), so the correction hands back to the fusion without a step.
   - **First run with path B on:** the shown cant was 0.07° off at 300 ms and 0.03° at 1 s. Without it, the fusion took about 3 s to get there.
   - That run had a bug: the first correction used the accelerometer value stored from before the sleep, so it started at 0 instead of the whole difference. Fixed in `v1.2.0-27-ge49c99e` (only samples from after the wake count); not yet run, because the turret unit went idle (below).
4. **Yaw does not wake the unit.** A 4.9° yaw step during sleep, like the 5° pitch steps and three quick back-and-forth pitch steps, left the BNO085 wake counter at 0. The stability detector works from the accelerometer, and the turret's movements are too gentle for it.
5. **The rig cannot wake its unit.** Over Wi-Fi an idle unit can't be reached, and the turret's movements don't wake it. The turret unit went idle during other work and stays so until someone touches it.

## What we will do

Depends on the decision below. With option A:
1. The game rotation vector keeps running while the hub sleeps (`alwaysOnEnabled`, non-wake, so its reports wait in the hub's buffer and never wake the host).
2. Proof on the turret, on battery, with `trace`:
   - wakes after no tilt and after tilts during sleep: the first reading within 0.1° of the settled one;
   - light sleep share and wake counts unchanged;
   - `state_change` over Wi-Fi.
3. If the owner can measure current: idle draw before and after.

## Decision for the owner

**How to make the first reading right:**
- **A. Keep the fusion running while the hub sleeps (recommended).** Proven on the unit, and a one-flag change in the code ADR-009 already has. The reading is right at the instant of the wake, including tilts made while asleep.
  - Cost by the datasheet: about 11 mA while idle, so up to 6% of the cell per 5 h idle stretch with the defaults. Idle is the time between stages, and the unit powers off after the sleep timeout.
- **B. Keep the power, correct the reading after the wake.** For example, take the cant from the accelerometer until the fusion agrees with it.
  - It costs nothing while idle, but it is new code in the level's path, the accelerometer is noisy while the rifle moves, and the first ~1° may come from the sensor's own restart. It would need its own investigation, with no guarantee.
- **C. Keep today's behaviour,** and accept up to about 1° off for the first second after every wake.
