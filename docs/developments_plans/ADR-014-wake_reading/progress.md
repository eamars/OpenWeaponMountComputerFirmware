# ADR-014 progress

Two units:
- **the turret unit:** `owmc-8626470`, on the OpenAutoTurret's pitch cradle (turret pitch is its cant), on battery, driven over the Wi-Fi remote control and updated by OTA through it;
- **the desk unit** (COM12 on 2026-11-09 and 10): on the owner's desk, upright on its edge, on USB.

Errors are those of the shown cant against each run's own settled value.

## Round 1, 2026-11-09: path B (`v1.2.0-32-g03dce0e`), since removed

In `digital_level_view_controller.c`, for 8 s after each wake the shown cant and pitch were the fusion's plus a correction toward the accelerometer's:
- the correction moved only on samples where |g| was within 0.3 m/s² of the unit's own 1 g (COM12's accelerometer reads 9.51 m/s²), started from the average of the first 5 such samples, then followed with a 0.3 s time constant;
- a dead band of 0.12° (half an accelerometer step); without it a small restart error got worse on COM12 (0.13° became 0.35° at 50 ms);
- a fade over the window's last 2 s.

Its checks on the turret (5° tilt during each 20 s sleep, timer wakes):

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

Also: `state_change` over Wi-Fi on the turret unit PASS (largest DMA block at least 90,112 B); light sleep 98.2% of idle.

It was closed, then failed the owner's test on 2026-11-10: a wake by motion on COM12 showed up to 78° wrong for 4.7 s, and about a second of overshoot (README, "What was tried").

## Round 2, 2026-11-10: a soft reset at rest after a wake by motion (`v1.2.0-36-g90c9b6a`), since removed

- Path B removed.
- The stability poller: after a wake by motion, the first "now still" report soft-reset the BNO085; only "left the still state" woke an idle unit.
- Measured beforehand with RAM-only restarts (README, the restart and motion-wake tables). The built firmware was flashed to COM12 only; the owner tried it the next morning ("better"). Its own six-pickup proof was not run: the next round replaced it.

## Round 3, 2026-11-10: the fusion on whenever the unit is on (`87456f6`, proven on `v1.2.0-38-g1ec2f29`)

- `digital_level_view_controller.c`: the game rotation vector is enabled at init and no longer switched with the tile. While idle the poller keeps the reading but draws nothing and skips recoil.
- `sensor_config.c`: the "Game rotation" switch starts or stops the report at once. `sensor_calibration_view.c`: leaving it no longer stops the report.
- `low_power_mode.c`: no `bno085_enter_sleep` / `bno085_wake_up` around idle; the reset at rest removed; only "left the still state" wakes (kept from round 2).
- `bno085.c/.h`: removed the sleep and wake calls, the soft reset, the wake profile and `bno085_set_report_always_on`.
- Remote control: `bno` removed; `trace` logs cant, pitch, the report count and idle. `rc_run.py`: the wake-profile scenario runs once, without always-on variants.
- Build: no warning from our code.

Both units were flashed over USB; the turret unit then ran on battery over the Wi-Fi remote control. No run logged a sensor reset.

**The turret unit, tilted 5° in the dark, woken by `idle 20`** (`trace 10` from the wake; error against the mean of the trace's last 3 s):

| Run | Turret | Level moved | First reading | 100 ms | 300 ms | 1 s | Worst after 300 ms |
|---|---|---|---|---|---|---|---|
| 1 | −4.87° | +4.87° | +0.01° | +0.02° | +0.03° | +0.00° | 0.13° |
| 2 | −4.90° | +5.00° | −0.16° | −0.16° | −0.17° | −0.14° | 0.18° |
| 3 | +4.96° | −4.80° | −0.10° | −0.10° | −0.09° | −0.09° | 0.10° |
| 4 | −4.92° | +4.73° | −0.25° | −0.26° | −0.25° | −0.25° | 0.27° |
| 5 | +4.94° | — | woken by motion 4 s after the tilt ("left the still state"), so not a timer wake | | | | |
| 6 | −4.94° | +4.62° | −0.12° | −0.11° | −0.12° | −0.07° | 0.12° |

Run 4 is 0.05° past the 0.2° bar, and the wake does not cause it:
- the report count rose at 49.4 a second through every dark spell, as awake: the fusion ran throughout;
- a trace straight through an idle (`trace 30 now`, a 5° tilt 7 s into the dark) shows the reading at −0.09° in the last dark sample and −0.08° in the first after the wake: no step;
- the residual is the fusion settling after the tilt, slower in the dark than awake: the same tilt awake, four times, was within 0.12° 2 s after the command and within 0.04° by 8 s (one run had a single 0.49° excursion 3 s after the command, back by 5 s).

**The desk unit (COM12), six pickups by the owner** (`motion_capture4.py`; error at the stability detector's "now still" report, against the trace's last 3 s):

| Pickup | At rest | Within 0.2° after | Note |
|---|---|---|---|
| 1 | +0.07° | at once | |
| 2 | +0.60° | 1.5 s | handled again after rest |
| 3 | +0.10° | at once | |
| 4 | +0.59° | 1.2 s | |
| 5 | +1.66° | — | handled again after rest and settled elsewhere: not counted |
| 6 | +0.12° | at once | |

Before (round 1 firmware): 13 to 14° off at rest, 6 to 8 s to come within 0.2°.

**The cost and no self-wake** (the turret unit on battery, `idle 570`, no trace armed): one idle of 569.9 s, ended by its timer. Light sleep 470.5 s, 82.6% (98.2% with the sensor asleep). 33,815 wakes by the sensor's interrupt (59 a second) and 1,911 by timers (3.4 a second).

**The tiles:** the level after 60 s on Settings, the turret tilted +4.87° meanwhile: 0.09° off at the first frame, steady.

**`scenario state_change` over Wi-Fi on the turret unit:** PASS, largest DMA block at least 90,112 B.
