# ADR-014 progress

All 2026-11-09, on two units:
- **the turret unit:** `owmc-8626470`, on the OpenAutoTurret's pitch cradle (turret pitch is its cant), on battery, driven over the Wi-Fi remote control and updated by OTA through it;
- **COM12:** still on the owner's desk, upright on its edge, on USB.

Errors are those of the shown cant against each run's own settled value (the fusion's average from 8.5 to 10 s after the wake).

## The instrument

- `trace <s> [now]` (remote control, RAM only): every 50 ms for s seconds from the next wake (or now), it logs:
  - the cant shown, the fusion's own, the accelerometer's, and the correction;
  - the pitch, the report counts and |g|.
- The unit keeps its log through the idle, and the host reads it once Wi-Fi is back.
- All 12 experiment runs kept every line: 201 to 202 lines a run, no `@drop`, every trace from t=0. The host reconnects about 4 s after a wake, and the 16 KB log buffer holds about 7 s of trace.

## The failure

After every wake the BNO085's fusion restarts somewhere between 0.01° and 1.2° off, even with no movement, and converges over 2 to 10 s. ADR-009 timed the first report after a wake (99 ms), not its value. The README has the first measurements.

## Options tried

| Option | Result |
|---|---|
| A: the game rotation vector kept running through sleep (`bno alwayson grv 1`) | Right at the wake (within 0.04°), tilts in sleep tracked. Costs at least about 8 mA while idle at any rate: the gyroscope alone is 7.5 mA (DS p.51) |
| A fresh fusion start: the report stopped before sleep and enabled after the wake | Still 0.64° off, same slow convergence |
| A full reset (`bnoreset`) | COM12: within 0.03°, after 0.75 s with no reading. Turret: 0.30° off, about 5 s to settle (the fusion takes its start from the accelerometer, vibration and all) |
| B, first build | Corrected within 0.1° by 300 ms on the turret. A bug: the first sample was the accelerometer value from before the sleep |
| B, first-sample fix (`v1.2.0-27-ge49c99e`) | Within about 0.07° by 1 s every time; one noisy single first sample made a start worse (0.55° at 50 ms) |

Also measured:
- Yaw, slow pitch steps and fast back-and-forth jogs do not wake the unit. The turret's "conservative" profile caps acceleration at 30°/s², about 0.05 m/s² at the unit, against the 0.78 m/s² the motion detector needs.
- The rig wakes its unit with `idle <s>` timers instead; the owner turned down a Wi-Fi exception.
- The accelerometer's cant moves in steps of about 0.23° (one count of its internal sensor at its range). On the still COM12 the steps show plainly.
- On the turret the yaw motor's vibration spreads it over about ±1° from one sample to the next (owner: the yaw motor).

## Built: path B in the level (`v1.2.0-32-g03dce0e`)

In `digital_level_view_controller.c`:
- **Window:** for 8 s after each wake (`bno085_dev->wake_us`), the shown cant and pitch are the fusion's plus a correction toward the accelerometer's.
- **Still samples only:** the correction moves only on samples where |g| is within 0.3 m/s² of the unit's own 1 g, followed slowly with a 2 s time constant. COM12's accelerometer reads 9.51 m/s², so a fixed 9.81 would not do.
- **Start:** the average of the first 5 still samples after the wake. Samples stored before the sleep don't count.
- **Then** a 0.3 s time constant.
- **Dead band:** the fusion is trusted within 0.12° of the accelerometer (half a step). Without it, path B made a small restart error worse on COM12 (0.13° became 0.35° at 50 ms).
- **Fade:** out over the window's last 2 s, handing back to the fusion without a step.
- **The accelerometer report** (`bno085_enable_accelerometer_report`, 20 ms) runs with the level's fusion report and stops with it. It is not always-on, so it stops by itself while the hub sleeps: idle costs nothing more.
- **`bno levelfix on|off`** (RAM only, on by default) compares with and without it.
- The experiments that lost (`bno wake fresh`, `bno accel`) are removed.

## Checks

**Turret, with the yaw motor's vibration** (5° tilt during each 20 s sleep):

| Run | Path B | Fusion alone at 50 ms | Shown at 50 ms | 100 ms | 300 ms | 1 s | Worst after 300 ms |
|---|---|---|---|---|---|---|---|
| tdb1 | on | 0.13° | 0.13° | 0.13° | 0.12° | 0.09° | 0.12° |
| tdb2 | off | 0.03° | 0.03° | 0.04° | 0.05° | 0.08° | 0.14° |
| tdb3 | on | 0.13° | 0.13° | 0.08° | 0.03° | 0.04° | 0.07° |
| tdb4 | off | 0.01° | 0.01° | 0.00° | 0.01° | 0.02° | 0.13° |
| tdb5 | on | 0.14° | 0.14° | 0.13° | 0.13° | 0.09° | 0.13° |
| tdb6 | off | 0.83° | 0.83° | 0.81° | 0.85° | 0.53° | 0.85° |
| tdb7 | on | 0.11° | 0.11° | 0.10° | 0.11° | 0.05° | 0.08° |
| tdb8 | on | 0.34° | 0.34° | 0.05° | 0.21° | 0.19° | 0.26° |
| tdb9 | on | 0.06° | 0.06° | 0.06° | 0.06° | 0.06° | 0.06° |
| tdb10 | on | 0.37° | 0.09° | 0.04° | 0.27° | 0.04° | 0.34° |

**COM12, still:** three runs on (fusion at 50 ms: 0.31°, 0.08°, 0.35°) showed at most 0.28° after 300 ms and 0.15° after 1 s; one run off, 0.15° and 0.13°.

**Verdict:**
- Without path B, a bad restart is most of the level's default threshold (1.0°) off for seconds: 0.85° here, 1.2° earlier.
- With it, the shown cant stays within a third of the threshold after 300 ms, even under the turret's vibration. Under that vibration the correction wanders by up to about 0.3° (runs tdb8 and tdb10): the accelerometer's noise through a 0.3 s time constant.
- When the fusion restarts close, path B leaves it alone (dead band).
- Longer smoothing could reduce the wander, but the vibration is the rig's, not the rifle's, so it is not tuned here.

**Also:**
- `state_change` over Wi-Fi on the turret unit, on battery (`rc_state_change_20261109_234155.log`): PASS, largest DMA block at least 90,112 B.
- Light sleep 98.2% of idle; the trace timer (20 wakes a second while armed) was on for most of the day's idles.
- No warning from our code.
