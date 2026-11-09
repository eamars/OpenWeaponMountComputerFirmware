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

## Round 3, 2026-11-10: the fusion on whenever the unit is on (`87456f6`)

- `digital_level_view_controller.c`: the game rotation vector is enabled at init and no longer switched with the tile. While idle the poller keeps the reading but draws nothing and skips recoil.
- `sensor_config.c`: the "Game rotation" switch starts or stops the report at once. `sensor_calibration_view.c`: leaving it no longer stops the report.
- `low_power_mode.c`: no `bno085_enter_sleep` / `bno085_wake_up` around idle; the reset at rest removed; only "left the still state" wakes (kept from round 2).
- `bno085.c/.h`: removed the sleep and wake calls, the soft reset, the wake profile and `bno085_set_report_always_on`.
- Remote control: `bno` removed; `trace` logs cant, pitch, the report count and idle. `rc_run.py`: the wake-profile scenario runs once, without always-on variants.
- Build: no warning from our code.

Proof: not run yet.
