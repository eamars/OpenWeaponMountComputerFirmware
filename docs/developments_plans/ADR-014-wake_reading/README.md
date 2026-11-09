# ADR-014: the level reads right the moment the unit wakes

Status: **Open** 2026-11-10. Path B was built and closed on 2026-11-09, then failed the owner's test of a wake by motion: COM12 showed up to 78° wrong for 4.7 s. PR #14 is on hold. The owner asked for a system-level analysis before fixing or dropping path B. It is below ("System-level analysis"), and the proposal waits for the owner's decision.

## What we want

When the shooter wakes the unit (lifts the rifle, touches the screen, presses the button), the level shows the rifle's real cant at once, not an angle that is wrong and creeps to the right one.

## Owner's direction (2026-11-09)

- "After sleep, it takes long time to recover." The unit was on battery.
- Debug it over Wi-Fi, on the unit mounted on the turret. To reproduce: the BNO085 asleep during light sleep, tilt, then wake.
- When the screen is off, Wi-Fi disconnects. Perhaps testing on the real unit needs exceptions.

### Second round (2026-11-09)

- Options before the safe path: does a slower rate cut A's cost; force a fresh measurement after a known wake; try path B. A only if the others fail or cost too much.
- Don't build a Wi-Fi exception for the rig: with full control of the turret, shake the unit until it comes back.
- Take path B, unless the log buffer can't capture the trace in real time. It can: every line kept in all 12 runs (progress).
- The noise on the turret is likely the yaw motor.
- A second unit on COM12, still on the desk, for a side-by-side comparison.

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

### Side by side: the turret unit and COM12 (still on the desk, upright), `v1.2.0-27-ge49c99e`

Error of the shown cant against each run's own settled value. Two COM12 runs lying flat are left out: with gravity along the screen's normal, cant is undefined.

| Run | Path B | 50 ms | 300 ms | 1 s | 2 s |
|---|---|---|---|---|---|
| turret, baseline | off | 0.56° | 0.57° | 0.33° | 0.14° |
| turret, fresh start | off | 0.62° | 0.61° | 0.40° | 0.23° |
| turret, after a yaw step | off | 0.08° | 0.08° | 0.08° | 0.05° |
| turret, base_a | off | 0.04° | 0.04° | 0.01° | 0.01° |
| COM12 | off | 0.44° | 0.41° | 0.27° | 0.16° |
| turret, path B (first build, with the stale-sample bug) | on | 0.28° | 0.06° | 0.02° | 0.04° |
| turret, pathB_a | on | 0.19° | 0.13° | 0.02° | 0.03° |
| turret, pathB_b | on | 0.55° | 0.19° | 0.07° | 0.13° |
| turret, pathB_c | on | 0.20° | 0.06° | 0.05° | 0.02° |
| COM12 | on | 0.21° | 0.21° | 0.05° | 0.04° |

- **Without path B,** the restart error varies from 0.04° to 1.2° across the day's runs. When large, it takes 2 to 4 s to clear.
- **With path B,** the shown cant is within about 0.07° by 1 s every time. Its first few hundred milliseconds are only as good as the accelerometer:
  - on a still unit, the accelerometer's cant moves in steps of about 0.23° (one count of its internal sensor at its range), and with no vibration to blur them, averaging can't get below a step;
  - on the turret, the yaw motor's vibration adds noise (one run, pathB_b, started from a noisy sample and was 0.55° off at 50 ms).
- **A full reset of the BNO085 (`bnoreset`):**
  - on COM12, it starts right: within 0.03°, after about 0.75 s with no readings;
  - on the turret, it starts 0.30° off and takes about 5 s, because the fusion takes its starting tilt from the accelerometer, vibration and all.
  - The only re-measure the SH-2 offers is this reinitialisation (Initialize command, SH-2 §6.4.4); nothing sets how much the fusion trusts the accelerometer.

## Path B (2026-11-09: built; dropping it is proposed below)

Path B corrected the shown cant toward the accelerometer for 8 s after a wake ([progress](progress.md)). The owner chose it over keeping the fusion on. Tested only with timer wakes on still units, it looked right. On a wake by motion it failed (F3 below), and it also swings past the truth (F4).

## System-level analysis (2026-11-10)

### Owner's direction

- Not in a hurry: look at it at system level, analyse the failure modes, generalise the use cases, then propose.
- Preference: after a wake, the IMU resets its previous measurement and starts afresh. Measure how long it takes to settle after a wake.
- Decide between fixing and dropping path B only after the full analysis.
- Use cases:
  1. Between matches the unit sits on a still rifle: it stays idle.
  2. Carried: walking easily exceeds the threshold, so it stays awake; the shooter turns it off to save power.
  3. On a stage: it stays awake (timer, recoil, moving between props).
  4. The shooter picks the rifle up and moves to the prop: the unit goes from idle to awake.
- Keep the stability detector's default threshold for now. Waking by a deliberate gesture (muzzle up, then down) is its own ADR.

### How the sensor restarts (measured, path B off)

Two still units, path B off. The reference is the fusion's own average over the second before the restart (`bno restart grv|reinit|soft|hard`, RAM only).

| Restart | COM12 (still): first reading, its error | Turret unit (still, yaw motor's vibration): first reading, its error, worst in 15 s |
|---|---|---|
| Resume after sleep (today) | 49 ms, 0.01–0.03° | 49 ms, 0.09–0.20°, 0.12–0.22° |
| Fusion report stopped and started | 350–550 ms, 0.00–0.01° | 50–600 ms, 0.00–0.02°, 0.08–0.13° (the fusion probably keeps its state: no different from resuming) |
| SH-2 Initialize command | no effect: no reset, readings carry on | — |
| Soft reset (command) | 450 ms, 0.01–0.04° | 100–400 ms, 0.01–0.23°, 0.28–0.53° |
| Hardware reset (pin) | 400 ms, 0.07–0.18° | 400–850 ms, 0.00–0.59°, 0.14–0.59° |

- **The floor:** with nothing restarted, the fusion on the still COM12 wanders 0.15 to 0.26° peak to peak over 30 s. Its 5 s averages stay within about 0.05° of the accelerometer's average. So "settled" means within about 0.2°.
- **A still wake** restarts within that floor on COM12, and up to about 0.2° on the turret. A reset is no better, and under vibration it is worse: the reset fusion takes its starting tilt from a shaking accelerometer.

### Wakes by motion (measured, path B off; COM12 picked up by the owner, swung as onto a prop, stood back on its edge)

"At rest" is the stability detector's "now still" report, 1.7 to 2.2 s after each pickup. Wake 5 is left out (the unit was moved again).

| Wake | At rest | +0.3 s | +1 s | +2 s | +5 s | Within 0.2° after rest |
|---|---|---|---|---|---|---|
| 1, resume | −14.4° | −10.0° | −6.3° | −3.9° | −1.1° | 8.1 s |
| 3, resume | −12.7° | −7.0° | −1.5° | −0.8° | −0.3° | 5.8 s |
| 2, soft reset at rest | (before the reset lands) | −0.15° | −0.17° | −0.16° | −0.15° | 0.27 s |
| 4, soft reset at rest | (before the reset lands) | −0.18° | −0.16° | −0.15° | −0.13° | 0.21 s |
| 6, soft reset at rest | (before the reset lands) | (reset still running) | −0.19° | −0.18° | −0.11° | 1.2 s |

### Failure modes

| # | Failure | Root cause | Seen |
|---|---|---|---|
| F1 | **After a wake by motion, the level is 13 to 14° off when the rifle comes to rest, and takes 6 to 8 s to come within 0.2°** | The fusion resumes from its pre-sleep orientation. The rifle turned before and while it woke (the detector, the wake, the gyroscope starting), so that rotation was never integrated. The game rotation vector then corrects tilt only slowly from the accelerometer. | Motion wakes on COM12; the owner's "takes a long time to recover" |
| F2 | After a still wake, 0.01 to 1.2° off, settling over seconds | The same restart with no rotation missed: a small error | Turret and COM12, timer wakes |
| F3 | Path B showed up to 78° wrong on a motion wake | Its "still" test used only \|g\|, so a turning unit passed; its 1 g reference learned from moving samples. It corrected a fusion that was right, with mid-swing accelerometer samples. | Owner's demonstration |
| F4 | Path B swings past the truth by 0.1 to 0.2° for about a second | Its correction trails the fusion's own convergence, so for a moment both correct the same error | Turret runs tdb8 and tdb10; the owner on COM12 |
| F5 | A reset under vibration starts up to 0.6° off | The fusion's starting tilt comes from the accelerometer at that moment | Turret unit only (motor vibration) |
| F6 | An idle unit wakes itself on a "now still" report | The stability poller wakes on any report, bit 0 ("entered still") included | ADR-013, 1 of 6 idles |
| F7 | A wake shows nothing new for about 0.4 s after a reset | A reset restarts the hub; the driver enables its reports again | Every reset |

### The use cases against the failures

| Use case | Wakes how | Failure today | With the proposal |
|---|---|---|---|
| 1. At rest between matches | No wake. A touch or the button wakes it still | F2 (small); F6 wakes it for nothing | Unchanged (no reset on a still wake); F6 fixed |
| 2. Carried | Stays awake, or wakes by motion over and over | F1 at each wake, while walking (not read then) | Reset when it next comes to rest |
| 3. On a stage | Awake: no restart | None: the fusion tracks motion while it runs | Unchanged |
| 4. Picked up and moved to the prop | Wake by motion | **F1: 13° off at rest, 6 to 8 s** | Right about 0.2 to 1.2 s after coming to rest (still detection 0.5 s, reset about 0.3 s) |
| Waiting at the start position, prone and still | Awake, still | None | Unchanged |
| A gesture wake (its own ADR) | By motion | F1 | Same as case 4 |

### What this means for path B

Path B corrects the symptom with a second estimate that has its own failure modes (F3, F4) and its own floor (the accelerometer's 0.23° steps). F1 is a stale starting point. A fresh start once the rifle is at rest removes it at the source, and needs no correction running afterwards. Proposed: drop path B.

## Proposal (waiting for the owner's decision)

1. **A fresh start at rest after a moving wake.** After a wake, if the unit moved (a "moving" report since the wake), the first "now still" report triggers a soft reset of the BNO085. The driver enables the reports and the calibration setting again, as it already does after any reset.
   - Still wakes (touch, button, timer) are not reset: they are already within the floor, and a reset could only add F5 and F7.
   - One reset per wake. Movement while awake is tracked by the running fusion and needs nothing.
2. **Path B, the wake correction and `bno levelfix` are removed.** The level shows the fusion as before ADR-014. `trace` and `bno restart` stay as debug commands.
3. **F6 fixed:** only "left the still state" (bit 1) wakes an idle unit.
4. **Proof:**
   - motion wakes by hand, about 6 with the reset and 2 without, on COM12, recorded with `trace`: within 0.2° by 1.5 s after the "now still" report, every time;
   - still wakes on both units: no reset, unchanged;
   - an idle unit left still for 10 minutes: no self-wake;
   - `state_change` over Wi-Fi.
5. **Not in this ADR:** the threshold (kept at its default), the gesture wake (its own ADR), and F5 (vibration while at rest is the turret's, not a rifle's).

