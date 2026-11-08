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

- **Automatic light sleep (step 2, item 3)**: left for a session with the owner (it cannot be seen on the bench, and a wake that fails on battery would leave a unit that does not come back at the line). Built and proven with the owner later the same day: "Step 2c" below.
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
- TS: there was no way to read it on the unit. The Power settings page's status now shows `Board` with the TH1 temperature (dashes outside the table), with USB and system on lines of their own (the USB line used to wrap mid-phrase). Seen on the unit: `Board 32.0°C` on USB. The owner: about 30 °C reads normal; no finer check possible without a reference thermometer. TS stays an input only (charging not gated).

## Step 2c: automatic light sleep while idle, with the owner (2026-11-08 15:00-16:05)

### What was built

- **Light sleep in idle only** (`idle_pm_config.light_sleep_enable`, `low_power_mode.c`). It is switched on only once the wake set-up below succeeds; active never sleeps.
- **ext1 arming removed.**
- **The four wake lines armed around each sleep** (`CONFIG_PM_LIGHT_SLEEP_CALLBACKS`). Touch GPIO15, BNO085 GPIO5, button GPIO0 and PMIC GPIO21 are armed with wake on low, and their CPU interrupts are off while armed, so a line held low cannot run its ISR again and again on the other core. On the way out each line gets its falling-edge interrupt back. A line still low has its interrupt raised again in software (`GPIO.status_w1ts`), so the event that woke the chip reaches its driver. A wake by a line that is high again by then is treated as a touch, since the touch controller is the only one of the four that pulses its line; that costs one I2C read.
- **Pins held through sleep.** ESP-IDF isolates every pin in light sleep (`CONFIG_ESP_SLEEP_GPIO_RESET_WORKAROUND` selects `CONFIG_PM_SLP_DISABLE_GPIO`): no input, no output, no pull. That would float the reset lines of the panel, the touch controller and the BNO085, the panel's chip select, the BNO085's PS0, the buzzer and both I2C buses, whose pull-ups are internal. These pins are kept as they are while awake (`gpio_sleep_sel_dis`). The wake lines are kept by the wake set-up itself.
- **No light sleep while USB power is in.** A PM lock (`usb_power`) is held from boot until the PMIC reports no VBUS, and taken again when it reports VBUS. A plug-in raises the PMIC interrupt, which is a wake line. ESP-IDF's own guard (`CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION`) was tried first and is not used; see below.
- **mDNS freed when the radio stops** (`wifi.c`). Its 100 ms timer kept running with Wi-Fi off and woke the chip ten times a second. It starts again with the next IP address, as it did before.
- **The serial remote control waits for input** (at most 500 ms) instead of polling every 20 ms (`rc_serial.c`).
- **rc `sleep`** (observer, `rc_run.py` knows it). It reports USB power, the time idle on battery, the time asleep and its share, the number of sleeps, and what ended each one (a wake line, with which line was low, or a timer). With `CONFIG_PM_PROFILING` it first prints the PM lock table to the console.
- Bluetooth checked first: NimBLE runs only during a Kestrel pull or search, and the controller is shut down after each (`nimble_port_deinit()`), so nothing of it runs while idle. A pull keeps the unit active.

### On the bench, plugged in

- First build, with `CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION` (`rc_ls_bench_20261108_152606.log`). The chip light-slept 3 times with the cable in: that option judges the connection by the host's 1 ms frame pulses, and they pause when the PC leaves the port idle. After that the USB console stopped answering for good. A touch woke the unit (the first touch wake from light sleep), but the console stayed dead until a replug.
- With the VBUS lock (`rc_ls_bench_20261108_153437.log`): idle on USB for 20 s gave 0 sleeps, and the lock table while idle shows only `usb_power` held. Idle with the port closed for 60 s, then reopened (`rc_ls_portclosed_20261108_153520_a/b.log`): the console answers, 0 sleeps in 91 s of idle.
- `state_change`: the first run (`rc_state_change_20261108_153627.log`) failed on no IP within 90 s. That was my set-up: I had left the unit idle (Wi-Fi off) before the run, and the scenario's first commands do not end idle. The rerun with the unit awake (`rc_state_change_20261108_154122.log`): PASS at a `largest_dma` floor of 70,000 B. The readings are 81,920 B awake and 73,728 B idle. The 8,192 B drop while idle is ESP-IDF's CPU retention buffer for light sleep with the CPU powered down, allocated when idle turns light sleep on and freed when idle ends (`sleep_cpu_configure()`). The display's staging buffers are allocated once at boot, so nothing that needs this RAM runs while idle. At the usual 80,000 B floor the run fails on the idle readings only.

### The owner's runs, unplugged

1. About 8 minutes, idle timeout 10 s (`rc_send_20261108_155047.log`). Woken 3 times each by touch, button, power key and picking up: every wake worked. Countdown running and held still past 10 s: the screen stayed on. Then 1 minute untouched. Counters: 1,455 sleeps, none refused; 76.6 s asleep; ended by a wake line 10 times, by a timer 1,445 times. The mean sleep was 53 ms, which led to the mDNS timer above. This build's idle counter mixed in the time idle on USB, so its share is not a figure.
2. With mDNS freed and idle counted on battery only, about 4 minutes (`rc_send_20261108_160213.log`). Left dark 2 minutes, then woken once by touch and once by picking up: both worked. **209.6 s idle on battery, 207.5 s asleep: 99.0%.** 2,179 sleeps; ended by a wake line 4 times (touch 1, BNO085 3), by a timer 2,175 times; about 1 ms awake per wake.

No wake without a cause was seen. The timer wakes are tasks' own deadlines, about 10 a second. Four are known: the LVGL task, the BNO085 driver's wait and the remote control each wake every 500 ms, and the low-power monitor every second. They cost about 1% of idle awake. The rest are not traced.

**Verdict: pass.** Every wake worked, 99% of idle on battery is spent asleep, the screen stays on with the timer running, and the unit stays awake and its console works while plugged in.

### Seen by the owner, not caused by light sleep

- **The timer at 0**: the screen goes dark the idle timeout after the timer ends (10 s in the test), as designed: the timer counts as activity only while it runs. Whether an expired timer should keep the screen on belongs to the countdown view (next ADR).
- **A short black blink while the unit was being moved (twice)**: the stability detector reports only when the unit changes between still and moving. So continuous movement longer than the idle timeout gives no activity, the unit goes idle, and the next "became still" report wakes it about half a second later. At the real 1 minute timeout this needs a minute of unbroken movement. The owner: fix after light sleep is proven; to be proposed as the next step.
