# ADR-009 progress

What was built for [ADR-009](README.md) and what the device showed. Times are NZDT.

## Step 0: measure before changing

### The profiling build (2026-11-08, branch `adr009_efficiency_power`)

Nothing changes for the user; all of it is off unless switched on.

- `WAKE_PROFILING_ENABLED` (`main/app_cfg.h`): one `wake_prof:` line a second after each wake from idle. Times in ms from the wake request: exit started, panel on, LVGL resumed, the first frame finished (`display_flush_arm_first_frame`). From the BNO085's wake command (`bno085_wake_up`, which now records it): the command's own time, the first game rotation vector, linear acceleration and stability reports, and how many of each came in the first 100 ms. The wake's cause is named by its caller (`low_power_mode_note_wake_cause`: press, motion, key, rc).
- `bno alwayson grv|la 0|1` (remote control, RAM only): whether the level's game rotation vector or linear acceleration keeps running while the hub sleeps (`bno085_set_report_always_on`). Today's firmware: rotation vector no, linear acceleration yes.
- `top [ms]`: each task's share of one core over the window (`mem_diag_cpu_report`), in builds with `CONFIG_FREERTOS_USE_TRACE_FACILITY` and `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS` set in the local `sdkconfig`.
- `scenario wake_profile` in `rc_run.py`: on the level tile, `top` active and idle, then for each of the four always-on combinations 8 idle periods of 10 s, each ended by `wake`, with medians printed.

Build: no new warnings in the changed files.

### Run (2026-11-08 12:59-13:07, `logs/rc/rc_wake_profile_20261108_125945.log`)

Unit still on the desk, level tile, rotation 0, Wi-Fi on. VERDICT PASS, 33 wakes profiled. Medians of 8 wakes each (min-max in brackets); ms from the wake request, sensor reports from the BNO085's wake command:

| Always-on while the hub sleeps | Panel on | First frame | First rotation report | Rotation reports in 100 ms | First lin. accel report | Lin. accel reports in 100 ms |
|---|---:|---:|---:|---:|---:|---:|
| Linear acceleration only (today) | 6.4 | 47.6 (24-64) | 22.8 (19-28) | 5 | 22.7 | 20 |
| Nothing (stability detector only) | 6.7 | 120.2 (22-140) | **98.6** (98.1-98.7) | 1 | **129.8** | 0 |
| Both | 6.5 | 52.4 | 23.8 | 16 | 23.8 | 6 |
| Rotation vector only | 6.5 | 46.8 | 22.1 | 20 | 47.6 | 2 |

Also: the wake request to the start of the exit 2.1 ms; the BNO085's wake command 0.3-0.9 ms.

What it shows:

- **The owner's observation holds.** With the hub's fusion off while it sleeps, the first rotation report comes 98.6 ms after the wake, every time (within 0.6 ms), and linear acceleration at 130 ms. With either fusion report kept running, the first report is simply the next one, about 23 ms. The difference is about 75 ms.
- **What the shooter sees:** the panel shows the last picture 6.5 ms after the wake request in every variant; the level moves again with the first frame drawn from a new report, about 48 ms today and about 120 ms with the fusion off.
- **A kept-on report delivers its sleep buffer at once**: 20 linear acceleration reports (or 16-20 rotation reports) in the first 100 ms instead of the 2 (or 5) its rate gives. Those are old samples; the queue keeps only the newest, so the level shows the latest of them.
- The cost of keeping fusion running is not in the datasheet: the stability detector alone is 0.13 mA, the accelerometer alone 1.4 mA at 125 Hz, full fusion 11 mA at 100 Hz (DS p.51). Linear acceleration needs the gyroscope as well, so it is in the milliamps.
- Two wakes came by themselves ("motion"), after 7.8 s and 1.1 s of idle, with nobody at the unit as far as the run knows: the stability detector at its default threshold on a desk. Watch for this in step 2 ("no wake without a cause").

### Found: LVGL does not stop while idle

`top` while idle: `taskLVGL` 3.6% of a core (1.1% while active and still), although idle "stops" LVGL. The cause is in `esp_lvgl_port` 2.7.2: `lvgl_port_stop()` disables LVGL's timers and stops the tick, but the port's task keeps looping, and LVGL 9.6's `lv_timer_handler()` returns 1 ms when timers are disabled (`lv_timer.c`, `lv_timer_run == false`). So the LVGL task wakes about every 2 ms, taking the LVGL lock each time, for as long as the unit is idle: some 500 wake-ups a second. That alone would keep the CPU out of light sleep. Fix for step 2: keep LVGL's timers enabled but every timer paused while idle, so the port task sleeps until an input event (touch and button still reach it), and the tick stays stopped. Some of the idle report's lines were lost on the console; the LVGL figure and the code agree.

CPU while active on the level tile, unit still (3 s window): both idle tasks 97-97.5%; `sensor_poller` 1.6% and 0.5% (two tasks of that name: the BNO085 driver's and the level's), `taskLVGL` 1.1%, `wifi` 0.5%, `esp_timer` 0.5%, the draw units 0.3%. The CPU is not the limit while the level is still; a moving level is for the step that changes the drawing.

### Unit state after the run

Profiling build left on the unit (wake profiling and run-time stats on), the always-on settings back to the firmware's (rotation no, linear acceleration yes), level tile, rotation 0 (the owner's choice), nothing saved.

## Steps 1 and 2, built while the owner was away (2026-11-08 13:20-14:30)

The owner, leaving for two hours: "do all your researches first then execute changes without my decision". Each change below was built, flashed and checked on the unit before the next. Nothing is committed (commits wait for the owner).

### Research that shaped the steps

- **Why the earlier BNO085 host wake likely failed.** The firmware arms `esp_sleep_enable_ext1_wakeup_io()` for the touch and BNO085 pins. In ESP-IDF v6.1 every sleep entry with ext1 armed routes those pads to the RTC function (`ext1_wakeup_prepare()` in `sleep_modes.c`) and nothing routes them back, so after the first light sleep their normal GPIO interrupts no longer fire: touch and sensor go silent. ext1 is a deep-sleep tool here.
- **The other way, GPIO wake-up, has its own trap.** `gpio_wakeup_enable()` sets the pin's interrupt type to the wake level (`gpio.c`), so a pin with an ISR fires it continuously while low. The button stays low as long as it is held, longer than the 300 ms interrupt watchdog. The wake level has to be armed only for the sleep itself and the edge restored on wake, with a low line checked once on the way out (an edge that happened in sleep is not seen again).
- **USB keeps the S3 out of light sleep.** USB Serial/JTAG does not work in or after light sleep on the S3 (`CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION` help text), so the unit must stay awake while a host is connected. That also means light sleep cannot be seen on the bench at all.
- **The panel's TE pulses stop in sleep-in**, which the flush relies on (below).

### Step 1: the PMIC (`main/pmic_axp2101.cpp`, `pmic_config.c`)

- Clean power off at low battery: two readings in a row on battery at 5% or less and under 3,600 mV (the gauge alone never switches a unit off).
- DCDC1's under-voltage and the over-voltage power off back on (the unused DC-DCs' under-voltage stays off); hard cut-off 2.6 V to 3.0 V.
- Interrupt: a low IRQ line counts as an interrupt on every pass (a missed edge no longer leaves the power key dead); only the status bits read are cleared.
- TS measured as an input only (reg 0x50 = 0x1A: not gating the charge, current on while converting, 50 µA), converted with the datasheet's table in log resistance; `pmic` shows `ts_raw` and `ts`.
- Termination 200 mA to 100 mA; the settings' default input limit 500 mA to 1000 mA (the owner's unit keeps its saved 2000 mA and 1000 mA charge).
- On the unit: the chip reads back 0x23 = 0x21 (DCDC1 UVP and OVP on), 0x24 = 0x04 (3.0 V), 0x30 = 0x0f (TS channel on), 0x50 = 0x1a, 0x63 = 0x14 (100 mA, termination on). `pmic`: TS 810 then 799 raw, 30.7 then 31.1 °C on a charging, USB-powered board (it moves; it used to be a frozen value). No interrupt repeats in the log.
- Not exercised: the low-battery power off (it needs a drained cell), the power key and USB plug events (they need hands), TS against a known temperature (needs the owner).

### Step 2a: quiet idle

- **LVGL really stops** (`low_power_mode.c`): every running LVGL timer paused while idle and timers left enabled, so the port's task sleeps until an input event; only timers that still exist are resumed. Touch and button wakes reach LVGL as before (`lv_indev_read()` does not look at the read timer).
- **Linear acceleration not always-on** (`bno085.c`): only the stability detector runs while the hub sleeps.
- **The level waits for each rotation report** (`digital_level_view_controller.c`, at most 500 ms, then it rechecks its run flag) instead of polling every 20 ms; so it also sleeps while idle.
- The once-a-second "Active duration" line to debug level; the PMIC read every 30 s while idle (2 s in use); automatic power-off default 1 h to 5 h of idle (the owner's unit keeps its saved choice).
- On the unit (`rc_wake_profile_20261108_133405.log`, PASS): while idle `taskLVGL` 0.0% (was 3.6%), both idle tasks 99.6-99.9%; active and still, `taskLVGL` 4.0% and the draw units 1.7% (a report every 20 ms now drawn as it comes). Wakes: first rotation report 98.6 ms, level moving 115 ms after the wake request (kept-on linear acceleration for comparison: 25 ms, 41 ms).

### Step 2b: the panel's own sleep while idle

- Display off then sleep-in (0x10) at idle entry; sleep-out (0x11), 60 ms, display on at wake, at least 120 ms after sleep-in (`set_display_sleep()` in `bsp.c`).
- First run (`rc_wake_profile_20261108_133948.log`): **one of 16 wakes ended in the LVGL watchdog's reboot.** The core dump: `taskLVGL` in `display_flush_wait_until()`, called from `display_flush_wait_scan()`, holding the LVGL lock. The cause: the TE interrupt took the time from the last pulse before idle to the first after it (about 10 s) as the panel's refresh period, and the scan-position rule scaled by it, so the flush waited about 4.7 s for a start point. Before step 2b the panel kept pulsing through idle, so no gap ever reached the rule. Fix (`display_flush.c`): a rise-to-rise time over 1.5 periods is a gap, not a period, and is not taken.
- With the fix (`rc_wake_profile_20261108_134543.log`, PASS, 16 of 16): TE pulses while idle 4 in 10 s (604 with display off only: the panel no longer scans); 85-90 in the second after a wake (scanning again). Panel on 66 ms after the wake request (was 6.5 ms); the level moving 117 ms after it (unchanged: the sensor's first report, 99 ms after its wake, is the later of the two).
- `state_change` regression: see below.
- Needs the owner's eye: the picture after a wake (sleep-in keeps the panel's memory and settings by the command set; a still unit's first frame redraws little).

### Not built, and why

- **Automatic light sleep (step 2, item 3)**: designed, not switched on. It cannot be seen on the bench (USB keeps the unit awake, and must), and a wake that fails on battery would leave a unit that does not come back at the line. To build with the owner: remove the ext1 arming; arm GPIO wake-up on low for GPIO15, 5, 0 and 21 only for each sleep (`esp_pm` light-sleep callbacks, `CONFIG_PM_LIGHT_SLEEP_CALLBACKS`), restoring the edges and checking the four lines on the way out; keep the four pins out of `CONFIG_PM_SLP_DISABLE_GPIO` (`gpio_sleep_sel_dis`); `CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION`; `CONFIG_PM_PROFILING` for the sleep share. Test: unplug, idle a few minutes, wake by touch, button, key and movement, plug in, read the counters.
- **Step 3.2, each level row painted once**: step 0 shows both cores 94-97% idle with the level shown, so the gain would be small, and its check needs the owner's eye. Left until a moving level shows the CPU matters.
- **Step 4**: the 1.2 s is inside the closed Wi-Fi library (`esp_wifi_init()`, 300 ms to 1,510 ms in every boot today). Starting the display first would change the order of internal-RAM allocations the display's DMA buffers depend on, a memory move the owner wants alone; for a boot every few hours at most, not now. The countdown settings write at boot was not traced to its caller (it comes after the first tile change; one NVS write a boot, which NVS wear-levels); the `gpio_install_isr_service` error is the touch component's own and harmless.

### Checks on the step 2 build, and the final build

- `state_change` on the profiling build (`rc_state_change_20261108_134940.log`): PASS, `largest_dma` 90,112 B at least, `taskLVGL` 9,188 B free.
- `scenario idle_cycles`, 10 minutes, profiling build (`rc_idle_cycles_20261108_135227.log`): PASS, 43 of 43 wakes profiled, every one with a frame (117.0-117.4 ms after the wake request), TE 4 pulses while idle and about 90 after each wake; no failure markers.
- Final build: `WAKE_PROFILING_ENABLED 0`, run-time stats and trace facility off again in the local `sdkconfig`; no new warnings in the changed files. `state_change --min-largest-dma 80000` (`rc_state_change_20261108_140757.log`): PASS. (With `--min-lvgl-stack` it fails only because this build has no stack report to read.) Unit left on the level tile, rotation 0, Wi-Fi on; `pmic` TS 32.2 °C while charging.

### For the owner, by hand

1. Wake by touch, by the button, by the power key and by picking the unit up: the screen comes back each time, and the level moves straight after.
2. The picture after a wake: no garbage, no black panel (the panel now sleeps while idle).
3. Countdown running and the unit held still past the idle timeout: the screen stays on.
4. Power off from the menu on battery, then on with the key (the BATFET path).
5. TS: `pmic` `ts=` against the room with the unit cool and off USB, and a finger on the board.
6. Automatic light sleep, when there is time together: unplugged, the test in "Not built" above.

### The owner's results (2026-11-08)

- Wake by movement: about 500 ms by the owner's feel, longer than before, and acceptable. The 117 ms above starts at the wake request; the time the stability detector takes to report the movement comes before it and was not measured.
- Wake by touch and by the button: more responsive than before, by the owner's feel.
- Countdown running: keeps the unit awake. Power key: works.
- TS: there was no way to read it on the unit. The Power settings page's status now shows `Board` with the TH1 temperature (dashes outside the table), with USB and system on lines of their own (the USB line used to wrap mid-phrase). Seen on the unit: `Board 32.0°C` on USB.
