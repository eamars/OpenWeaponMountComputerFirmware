# ADR-014: the level reads right the moment the unit wakes

Status: **Built** 2026-11-10, third round: the BNO085's fusion runs whenever the unit is on, through idle and on every tile. Proven on both units ([progress](progress.md)): no step at the wake, no reset, no self-wake; the chip light-sleeps 82.6% of an idle.

## What we want

When the shooter wakes the unit (lifts the rifle, touches the screen, presses the button), the level shows the rifle's real cant at once, not an angle that is wrong and creeps to the right one.

## Owner's direction

**2026-11-09, first round:**
- "After sleep, it takes long time to recover." The unit was on battery.
- Debug it over Wi-Fi, on the unit mounted on the turret. To reproduce: the BNO085 asleep during light sleep, tilt, then wake.

**2026-11-09, second round:**
- Look at the options before the safe path: does a slower rate cut the cost of keeping the fusion on; force a fresh measurement after a known wake; try path B. Keep the fusion on only if the others fail or cost too much.
- Don't build a Wi-Fi exception for the rig.
- The noise on the turret is likely the yaw motor.

**2026-11-10, system level** (after path B failed a wake by motion):
- Look at it at system level: failure modes, use cases, then a proposal. Preference: after a wake the IMU drops its previous measurement and starts afresh. Measure how long it takes to settle.
- Use cases:
  1. Between matches the unit sits on a still rifle: it stays idle.
  2. Carried: walking easily exceeds the threshold, so it stays awake; the shooter turns it off to save power.
  3. On a stage: it stays awake (timer, recoil, moving between props).
  4. The shooter picks the rifle up and moves to the prop: the unit goes from idle to awake.
- Keep the stability detector's default threshold. A wake by a deliberate gesture (muzzle up, then down) is its own ADR.
- Approved: a fresh start (soft reset) once the rifle comes to rest after a wake by motion; path B dropped.

**2026-11-10, third round:**
- The owner tried the reset at rest in the morning: better than the day before.
- The owner's idea: a state between awake and asleep, with the screen off and the IMU still running, so a quick pickup needs no reset.
- Then, on the analysis below: no asleep state at all. The sensor stays on until the unit powers off.
- Rework this ADR to that, remove the code it makes dead, and record what was learned. No reasoning in the source code.

## What we know

### The cause

The BNO085 runs its fusion only while a fused report (game rotation vector, linear acceleration) is on. Until this round it stopped in two places:
- **in idle,** because the hub was put to sleep with only the stability detector running (ADR-009, for power);
- **on every tile but the level,** because the level switched its reports off when it was not shown.

When the fusion starts again it resumes from its last orientation and corrects tilt from the accelerometer only slowly. Restarting the report or resetting the hub does not help: a reset starts from the accelerometer at that moment, which is wrong while the rifle moves or shakes.

### A still wake (measured on the turret, 2026-11-09)

The unit on the turret's pitch cradle (turret pitch is its cant), on battery, followed over Wi-Fi with `trace`.

| | Before sleep | 50 ms after the wake | 1 s | 2 s | 4 s | 10 s |
|---|---|---|---|---|---|---|
| Tilted 5° during sleep (settles at −91.3°) | −95.8 | −90.09 (1.2° off) | 0.9° off | 0.3° off | 0.12° off | settled |
| Not tilted (settles at −91.2°) | −91.3 | −90.50 (0.7° off) | 0.6° off | — | 0.2° off (3 s) | settled |

Across the day's runs the restart error ranged from 0.01° to 1.2°, taking 2 to 4 s to clear when large. ADR-009 had timed the first report after a wake (99 ms), not its value.

### A wake by motion (measured on COM12, 2026-11-10)

The owner picked the unit up, swung it as onto a prop and stood it back on its edge. "At rest" is the stability detector's "now still" report, 1.7 to 2.2 s after the pickup.

| Wake | At rest | +0.3 s | +1 s | +2 s | +5 s | Within 0.2° after rest |
|---|---|---|---|---|---|---|
| 1, resume | −14.4° | −10.0° | −6.3° | −3.9° | −1.1° | 8.1 s |
| 3, resume | −12.7° | −7.0° | −1.5° | −0.8° | −0.3° | 5.8 s |
| 2, soft reset at rest | (before the reset lands) | −0.15° | −0.17° | −0.16° | −0.15° | 0.27 s |
| 4, soft reset at rest | (before the reset lands) | −0.18° | −0.16° | −0.15° | −0.13° | 0.21 s |
| 6, soft reset at rest | (before the reset lands) | (reset still running) | −0.19° | −0.18° | −0.11° | 1.2 s |

The rotation made before and while the unit woke was never integrated, so the fusion resumed 13 to 14° off.

### Keeping the fusion on (measured on the turret, 2026-11-09)

With the game rotation vector kept running while the hub slept (a RAM-only switch, since removed):
- not tilted: −91.17° before sleep, −91.21° at the wake, −91.20° 50 ms later;
- tilted 5° during a 30 s sleep: −95.75° at the wake, the right value. The tilt was tracked while dark;
- the unit still light-slept through the idle.

### How the sensor restarts (measured on two still units, 2026-11-10)

The reference is the fusion's own average over the second before the restart.

| Restart | COM12 (still): first reading, its error | Turret unit (yaw motor's vibration): first reading, its error, worst in 15 s |
|---|---|---|
| Resume after sleep | 49 ms, 0.01–0.03° | 49 ms, 0.09–0.20°, 0.12–0.22° |
| Fusion report stopped and started | 350–550 ms, 0.00–0.01° | 50–600 ms, 0.00–0.02°, 0.08–0.13° |
| SH-2 Initialize command | no effect: no reset, readings carry on | — |
| Soft reset (command) | 450 ms, 0.01–0.04° | 100–400 ms, 0.01–0.23°, 0.28–0.53° |
| Hardware reset (pin) | 400 ms, 0.07–0.18° | 400–850 ms, 0.00–0.59°, 0.14–0.59° |

- Stopping and starting the report does not reset the fusion: it carries its state.
- **The floor:** with nothing restarted, the fusion on the still COM12 wanders 0.15 to 0.26° peak to peak over 30 s; its 5 s averages stay within about 0.05° of the accelerometer's. "Settled" means within about 0.2°.

### What it costs

From the BNO085 datasheet (DS p.51); there is no current meter here.
- The fusion: 7.50 mA on VDD at every listed rate (the gyroscope), plus 3.50 / 5.34 / 6.90 mA on VDDIO at 100 / 200 / 400 Hz. A slower report rate can only cut the smaller part, so it costs at least about 8 mA at any rate, and about 11 mA at 100 Hz.
- The accelerometer at 125 Hz: 1.4 mA. The stability detector: 0.13 mA. ADR-009 estimated the whole idle, sensor asleep, at a milliamp or two.
- **The ESP32's share, measured** (the turret unit on battery, a 570 s idle, 2026-11-10): the chip light-slept 82.6% of the idle, against 98.2% with the sensor asleep. The sensor's interrupt woke it 59 times a second (the game rotation vector at 50 Hz and the stability detector at 10 Hz), against 3.4 timer wakes a second. Its current while awake is not measured here; at an assumed 20 to 30 mA, the extra 16% awake adds 3 to 5 mA. The fuel gauge's 1% steps are too coarse to check it over 10 minutes.

With the sensor on through idle, about 12 mA for the sensor and an estimated 15 to 17 mA in all:

| Case | Extra drain | Share of the 950 mAh cell |
|---|---|---|
| A 30 min break between stages | about 8 mAh | 0.8% |
| A match day, 12 such breaks | about 90 to 100 mAh | 10% |
| Left dark until the default power-off (5 h) | about 75 to 85 mAh, then off | 8% |
| Power-off set to Never | flat in about 2.5 days dark, against about 3 weeks | — |

### Other findings

- **What wakes the unit:** yaw, slow pitch steps and quick back-and-forth jogs on the turret do not. The turret's "conservative" profile caps acceleration at 30°/s², about 0.05 m/s² at the unit, against the stability detector's 0.78 m/s².
- **A "now still" report woke an idle unit** lying still (ADR-013, 1 of 6 idles): the stability poller woke the unit on any report.
- **The accelerometer's cant moves in steps of about 0.23°** (one count of its internal sensor at its range). On the turret the yaw motor's vibration spreads it over about ±1° from one sample to the next.
- **The instrument:** `trace` (remote control, RAM only) logs the level's cant every 50 ms from the next wake. The unit keeps its log through the idle and the host reads it once Wi-Fi is back. Every line was kept in every run; light sleep was 98.2% of idle with it armed (20 timer wakes a second).
- **Not explained:**
  - one idle on the turret ended 4 to 5 s after a tilt by a touch wake, with nobody touching the unit as far as the run knows;
  - on 2026-11-10 one of seven 5° turret tilts in the dark woke the unit by "left the still state", about 4 s after the tilt command.
- **After a tilt in the dark the fusion settles more slowly than awake** (the turret, 2026-11-10): 0.1 to 0.25° off 10 to 14 s after a 5° tilt in the dark, against 0.05° or less 8 s after the same tilt awake. The reading has no step at the wake. The cause is not known.

## What was tried

| Option | Result | Kept? |
|---|---|---|
| Restart the fusion report after the wake | Still 0.64° off, same slow convergence: the fusion keeps its state | No |
| Reset the hub at the wake | Starts from the accelerometer at that moment: 0.3 to 0.6° off under vibration, 0.4 to 0.85 s with no reading | No |
| Path B: for 8 s after a wake, correct the shown cant toward the accelerometer while still ([progress](progress.md)) | Within 0.07° by 1 s on still timer wakes. On a wake by motion up to 78° wrong for 4.7 s: its "still" test used only \|g\|, so a turning unit passed, and its 1 g reference learned from moving samples. It also swung past the truth by 0.1 to 0.2° for about a second | No: a second estimate with its own failures |
| A soft reset once the rifle comes to rest after a wake by motion | Within 0.2° 0.2 to 1.2 s after coming to rest. Wrong while moving; no reading for about 0.4 s; needs the rifle to be still enough for the detector, which a rifle held on a prop may never be | No: replaced by the next row |
| A screen-off state with the sensor on for a while, then the sensor asleep (owner's idea) | The pickup within the window is right at once; after it, the reset at rest again | Folded into the next row |
| **The fusion on whenever the unit is on** | No step at the wake; tilts while dark tracked. An estimated 15 to 17 mA while dark ([progress](progress.md), round 3) | **Yes** |

## Failure modes

| # | Failure | Root cause | Now |
|---|---|---|---|
| F1 | After a wake by motion, 13 to 14° off at rest, 6 to 8 s to come within 0.2° | The fusion stopped while dark and resumed from its old orientation | Gone: the fusion never stops |
| F2 | After a still wake, 0.01 to 1.2° off, settling over seconds | The same restart, with no rotation missed | Gone |
| F3, F4 | Path B: 78° wrong on a motion wake; overshoot | Path B | Path B removed |
| F5 | A reset under vibration starts up to 0.6° off | A reset starts from the accelerometer | No reset is made |
| F6 | An idle unit wakes itself on a "now still" report | The poller woke on any report | Fixed: only "left the still state" wakes |
| F7 | No reading for about 0.4 s after a reset | A reset restarts the hub | No reset is made |
| F8 | The level shows a stale angle after another tile, if the unit moved meanwhile | The level's reports stopped with the tile | Gone: the game rotation vector runs whatever the tile |

## The use cases

| Use case | Before this ADR | Now |
|---|---|---|
| 1. At rest between matches | Idle, sensor asleep | Idle, sensor on: an estimated 15 to 17 mA instead of 1 to 2 |
| 2. Carried | Stays awake | Unchanged |
| 3. On a stage | Awake, the fusion running | Unchanged |
| 4. Picked up and moved to the prop | 13 to 14° off at rest, 6 to 8 s | 0.07 to 0.6° at rest, within 0.2° by 1.5 s at most: as after the same handling while awake |

## What we do

1. **The game rotation vector runs from init** and is not switched with the level tile or by idle. The Advanced page's "Game rotation" switch still starts and stops it, at once. Leaving the sensor calibration keeps it as that switch has it.
2. **The hub is not put to sleep in idle.** While the screen is off, the level keeps reading the sensor but draws nothing and does not act on recoil. Linear acceleration still runs with the level tile.
3. **Only "left the still state" wakes an idle unit** (F6).
4. **Removed:** path B; the reset at rest and the driver's soft reset; the hub's sleep and wake calls; the driver's wake profile (`wake_prof` keeps its display timings); the remote control's `bno` command (`alwayson`, `restart`) and the wake-profile scenario's variants. `trace` and `bnoreset` stay.
5. **No change to saved settings,** the stability detector's threshold or the timeouts.

## How the unit shows it works

- **The turret unit, on battery:** tilted 5° by the turret while dark, woken by `idle <s>`: the first reading after the wake within 0.2° of the settled value, every time.
- **The desk unit, picked up by the owner** (as onto a prop, then stood on its edge): right from the first reading after coming to rest, with no reset in the log.
- **The cost:** the light-sleep share of a 10-minute idle on battery, against 98.2% with the sensor asleep (2026-11-09). Pass if the chip still sleeps most of the time; then the drain is close to the sensor's own 8 to 11 mA. If it is far higher, a slower report rate while dark is tried first.
- **No self-wake:** an idle unit left still for 10 minutes.
- **The tiles:** the level after a minute on another tile with the unit tilted: right at once.
- `scenario state_change` over Wi-Fi on the turret unit: PASS.

## Not in this ADR

The stability detector's threshold (kept at its default), the gesture wake (its own ADR), and a shorter default power-off (not needed at an estimated 15 to 17 mA, by the table above).
