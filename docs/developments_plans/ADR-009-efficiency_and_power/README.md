# ADR-009: efficiency, low power and quick wake

Status: **Open** 2026-11-08. Approved by the owner. Built while the owner was away ("execute changes without my decision"), each change checked on the unit: step 0 (measure), step 1 (PMIC), step 2 (LVGL, sensor and tasks quiet while idle; the panel's own sleep), committed daac843 on `adr009_efficiency_power`, not pushed. Automatic light sleep (step 2, item 3) built and proven with the owner, unplugged: every wake works, 99% of idle on battery asleep, committed on the same branch. A moving unit now counts as in use, so it no longer blinks dark in the hand (built and checked with the owner, not committed yet). The level's drawing measured while moving against `main`: no regression (not committed yet). Left: the owner's remaining hands-on checks; steps 3.2 and 4 set aside with reasons ([progress](progress.md)). The facts below come from the code, the unit's own readings, earlier logs and the datasheets in `OpenWeaponMountComputer/pcb/datasheet` (page numbers are the printed ones).

## What we want

The unit runs on a battery. While nobody uses it, it should draw as little as it can and still wake quickly when it is picked up, touched or its button pressed. While it is in use, it should cost no more than it needs. The power code (low power, BNO085, PMIC) should follow each chip's documented good practice.

## Owner's direction (2026-11-08)

- Is LVGL running in the most efficient way: the general set-up, the digital level's drawing, and so on.
- Is the low-power design functional and practical? When idle, the unit should be in a low-power state, but wake quickly.
- Wake by motion is part of the design. The BNO085 can wake the host without polling; the owner tried that before and it did not work well. Look into the BNO085's low-power integration.
- The level's full-screen colour is the design: in a match the shooter must see the tilt at a glance, so it stays big and bright.
- There is no battery-side current meter. Design on good practice. The owner has little faith in the PMIC code: check it too.
- Any other optimisation worth making.

How the unit is used, which the low-power design serves (owner, 2026-11-08):

- Carrying is not the case to design for: the user switches the unit off, or it switches itself off.
- The low-power state is for **between stages**, while the rifle is not in use.
- The level is in normal use at the stage, and in the moments before it. Between waiting and shooting on the line the unit must not switch itself off; it must be responsive.
- **Sleeping during shooting must be strictly avoided.**
- The power key's long hold stays a restart.
- The cell is a Vapcell T9 (16340, 950 mAh, unprotected, 4.20 V, not below 2.5 V, by the retailers' listings). The termination current was set to the highest because some units charged slowly.
- TS was switched off because TH1 seemed to read a constant temperature, far from right.

Standing rules: performance changes are risky, so they go in one at a time, each proven on the unit before the next. The code around tearing is deliberate (ADR-002). No long soaks; cycle tests of at most 10 minutes. No flash encryption.

## What we know

### The power states today (checked in the code)

| State | Entered | Screen | CPU | Radio | Sensor | Wakes on |
|---|---|---|---|---|---|---|
| Active | boot, any wake | on | 80-240 MHz, scaled by load | Wi-Fi as set | reporting (level at 50 Hz) | — |
| Idle | no activity for the idle timeout (default 5 min) | "display off" (0x28), LVGL stopped | fixed 80 MHz, **never sleeps** | Wi-Fi off | BNO085 asleep, stability detector on | touch, button, power key, movement |
| Off | the sleep timeout while idle (default 1 h) | off | off (PMIC power off) | off | off | power key, then a full boot (about 3 s) |

`enter_sleep_mode()` in `main/low_power_mode.c` (light sleep) is never called; the sleep timeout powers the unit off instead.

### What keeps the unit awake while shooting (checked in the code)

- Activity is a touch, the button, the power key, USB in or out, and each stability-detector report ("became stable", "stopped being stable"). Linear acceleration and the level's own reports do not count. Since step 2d, a unit the detector last reported as moving also counts as active on every 1 s poll, until it reports still.
- The update and the sensor calibration block idle (`prevent_idle_mode_enter`), and **the running countdown timer counts as activity on every tick** (`countdown_timer.c`), so the screen cannot go dark during a timed stage. In practice the idle timeout is 1 min (owner; 10 s is a test setting, listed last for that reason; the code's default is 5 min), counted from the timer's end, the last touch or the last movement. (A first reading of the code missed the timer's activity; corrected 2026-11-08, nothing to change.)
- The timeouts are counted by a 1 s poll, so idle comes up to 1 s after the timeout.

With idle drawing a milliamp or two instead of tens (below), the automatic power-off no longer has to come early to save the battery: 1 h of idle at 2 mA is 2 mAh of a 950 mAh cell. It can wait much longer, so a long wait on the line between stages does not end with the unit switched off.

### Motion wake does not need the CPU awake

Motion wake already works without polling: the BNO085's stability detector is configured as a wake sensor that keeps running while the hub sleeps (`wakeupEnabled`, `alwaysOnEnabled`), and its task blocks on the report. The CPU is kept awake by other things, none of which motion wake needs:

- the CPU itself: `idle_pm_config` is 80 MHz with `light_sleep_enable = false`, so between tasks it only waits;
- the level's display loop runs every 20 ms while idle (`digital_level_view_controller.c`): its run flag follows the tile, not idle, so it polls a sleeping sensor 50 times a second;
- the serial remote control polls every 20 ms when on (`rc_serial.c`); the BNO085 driver's wait wakes every 500 ms, the PMIC task every 2 s, the low-power monitor every second with a log line.

### BNO085 (datasheet `BNO080_085-Datasheet.pdf` = DS, `SH-2-Reference-Manual.pdf` = SH2)

What good practice looks like, from the documents:

- The hub's sleep command keeps running only sensors flagged always-on or wake (DS p.23). A wake sensor interrupts the sleeping host; an always-on, non-wake sensor keeps running into a circular buffer that is delivered at the next wake (SH2 p.35).
- Costs (DS p.51): idle 0.13 mA; stability detector 0.13 mA (no more than idle); tap 0.28 mA; shake 0.46 mA; significant motion 0.54 mA; stability classifier 10.4 mA; fusion at 100 Hz 11 mA.
- The stability detector uses the accelerometer only and reports "became stable" and "stopped being stable" (SH2 p.23). Its threshold and duration are an FRS record (0xED85; defaults 0.784 m/s² and 0.5 s, SH2 p.79-80), so how much movement wakes the unit can be set.
- H_INTN stays low until the host addresses the chip; unanswered, it times out after about 10 ms and asserts again (DS p.19, p.48). A missed falling edge is therefore retried by the sensor; the driver's edge-triggered interrupt is safe for normal running.
- The datasheet asks for H_INTN on a GPIO that can wake the host (DS p.13). GPIO5 can.

What our code does differently:

1. **Linear acceleration is always-on** (`bno085_enable_linear_acceleration_report`, `alwaysOnEnabled = true`). It is on whenever the level tile is shown, so on the level tile, the usual place to go idle, the hub keeps running linear acceleration (a fused output) at 20 Hz through its sleep, for reports nobody reads. The datasheet gives no figure for it; fusion at 100 Hz is listed at 11 mA, so this is likely the largest avoidable drain while idle. The fix is one flag.
2. **The stability detector runs at its default threshold** (0.784 m/s², 0.5 s). For the match case that is what we want: picking the rifle up wakes the unit. How long from the first movement to the first frame is not measured; that is the number that says whether the wake is quick enough when the shooter steps up to the line.
3. For the CPU to sleep, the wake has to come from the GPIO wake-up path, which is level-triggered (wake on low). Edge interrupts do not wake the S3 from light sleep. Using the same pin for both needs care: while asleep, the pin wakes on low; once awake, the normal edge interrupt services it. A likely reason the earlier attempt went badly is a mix of the two (an interrupt firing again and again on the level, or a wake that never came because the pin was edge-only); the owner may remember which.
4. Small: the I2C HAL never fills the report timestamp (`t_us`), so SH-2's host timestamps are garbage. Nothing uses them today.

### PMIC: AXP2101 (`AXP2101_C3036461.pdf`, the FAQ and design guide next to it)

Checked against the datasheet; what is right and what is not:

| Setting in `pmic_axp2101.cpp` | What the datasheet says | Verdict |
|---|---|---|
| Unused DC-DCs and LDOs off | The board uses DCDC1 only (3.3 V for everything; the PCB has one +3V3 rail) | Right |
| `disableBATFET()` then `shutdown()` | Reg 0x12 bit 3 is "BATFET enable when power-off and battery only"; the 40 µA off current is given with it off; the power key turns it on again (p.11, p.18, p.33) | Right: lowest off current. The FAQ names a different bit (0x12 bit 2); the unit coming back on with the key after an automatic power-off is the test |
| Low battery: warning 10%, "shutdown" 5% (reg 0x1A) | Both levels only raise an interrupt; battery level is not a power-off source (p.20, p.28, p.36) | **Wrong in effect**: our handler only logs the level-2 interrupt, so nothing powers off at 5%. The battery runs on to the hard cut-off below |
| `setSysPowerDownVoltage(2600)` (reg 0x24) | Range 2.6-3.3 V; this is the chip's hard cut-off (p.20, p.37) | The lowest setting: with nothing acting at 5%, the cell is run down to 2.6 V, below the usual 3.0 V a Li-ion cell should stop at, and the 3.3 V rail has long dropped out before that |
| `disableDC1LowVoltageTurnOff()` and `disableDCHighVoltageTurnOff()` (reg 0x23) | Turns off "PMIC off when DCDC1 drops below 85%" and the over-voltage cut-off (p.20, p.36-37) | **Wrong for DCDC1**: the comment says "unused outputs", but DCDC1 powers the whole board. Without it a sagging 3.3 V gives brown-out resets instead of a clean power off |
| `disableTSPinMeasure()` ("FIXME") | TS drives a current (reg 0x50 bits 1:0: 20, 40, 50 or 60 µA) into a 10 kΩ NTC and the ADC reads the voltage at 0.5 mV a step; out of the window, charging pauses (charge about 0-45 °C) (p.25-26, p.45) | The board has the 10 kΩ NTC on TS (TH1, 0603 on the PCB, so it reads the board, not the cell). With TS off there is no charge temperature guard, including charging below 0 °C, which damages Li-ion cells. Why TH1 looked wrong, below |
| Termination 200 mA (`ITERM_200MA`), precharge 100 mA, charge 500 mA, 4.2 V, input limit 500 mA | Termination 0-200 mA, default 125 mA (p.48) | For a 950 mAh cell, about a tenth (100 mA) is usual. A higher termination does not charge faster; it stops the charge earlier, short of full. The input limit is what slows charging: 500 mA from USB is shared with the running unit, so the cell gets what is left |
| Power key: long press restart (`enableLongPressShutdown` + `setLongPressRestart`, reg 0x22), 10 s; 16 s PMIC off disabled | Reg 0x22 bit 1 enables the long-press action, bit 0 = 1 makes it a restart (p.36) | Right: holding the key restarts the unit (owner: keep it). The menu's Power off is the way off by hand |
| Interrupt: falling edge, `clearIrqStatus()` writes all ones | The pin stays low until the status is cleared; bits are write-1-to-clear (p.29, p.43) | Two gaps: an event arriving between the read and the clear-all is lost (for example a key press); and a missed edge leaves the pin low for good, so no more interrupts (the power key would go dead), since the 2 s timeout reads the status but not the interrupt flags |
| `ts_temperature` read every 2 s | TS measurement is off | Meaningless value (see below) |
| Status read every 2 s, also while idle | | Fine while active; needless while idle |

**Why TH1 read a constant, wrong temperature.** Both are explained by the code, not by the part:

- *Constant:* `disableTSPinMeasure()` switches the TS current source and its ADC channel off (XPowersLib writes reg 0x50 bits 3:2 = 00 and clears the ADC channel bit). The register then keeps its last value, and `getTsTemperature()` converts that stale number every 2 s.
- *Wrong, with it on:* XPowersLib's `enableTSPinMeasure()` writes reg 0x50 bits 1:0 = 11, which is 60 µA, while its `getTsTemperature()` converts as if 50 µA flowed. The resistance comes out 20% high, which reads about 4-5 °C too cold near room temperature. Its Steinhart-Hart constants are generic; the datasheet's own table (p.26, for a TH11-3H103F) is the better reference unless TH1's B value is known.
- *By design:* TH1 sits on the PCB, near the PMIC, so it reads the board (warmer while charging), not the cell.

So TS can be trusted once we set the 50 µA source ourselves and convert with the datasheet's table. A first build reads it with TS as an input only (reg 0x50 bit 4 = 1, charging not gated) and compares it with the room and a finger on the board; only then does it guard the charge.

Also: the PMIC and the touch controller share I2C0. The FT3168 datasheet warns that in its low-power modes an I2C transfer to another device on the bus can upset it (`ft3168.pdf` p.7); its firmware recovers by itself. The PMIC's 2 s reads are such transfers. Not known to cause trouble today; worth knowing if touch wake ever misbehaves.

### The panel and touch

- The panel module (DO0164) names its driver as ICNA3311 / CO5300 compatible (DO0164 p.3); we drive it with the SH8601 driver, which uses the same standard commands.
- Currents (DO0164 p.9): full white at 350 nits, about 55 mA in all; sleep-in about 0.6 mA. "Display off" alone is not given. The documented power-down is display off, then sleep-in (p.20-21); we stop at display off. Coming back needs sleep-out and at least 50 ms before display on (our init waits 80 ms).
- Touch (FT3168 p.6, p.9): active 1.5 mA, monitor 30 µA (scans slowly, switches to active on a touch). Whether it enters monitor mode by itself is not stated.

### What idle could cost, by the datasheets

Today's idle is the S3 at 80 MHz waiting (tens of milliamps by Espressif's figures; that datasheet is not in the folder), the BNO085 running linear acceleration, and the panel in display off. Following the datasheets: S3 in light sleep (a fraction of a milliamp), panel in sleep-in (0.6 mA), BNO085 stability detector only (0.13 mA), touch in monitor (0.03 mA), plus the PMIC, the PSRAM and the regulator's own loss, none of which are documented here. That is one to two orders of magnitude less, which matters more than any other change in this ADR. Off is 40 µA by the PMIC datasheet. Without a meter these are estimates, not measurements.

### LVGL set-up (checked in `sdkconfig` and `lvgl_display.c`)

Most of it was tuned in ADR-002 and holds up: two full-frame RGB565 draw buffers in PSRAM, partial render, two software draw units (both cores render), our own flush with internal DMA staging and a TE-synced start, `-O2`. The LVGL task sleeps until its next timer; a still screen draws nothing.

- The ESP32-S3 assembly blending in `esp_lvgl_port` is compiled only for LVGL 9.1 (`esp_lvgl_port/CMakeLists.txt`); on our 9.6 it is not used.
- About a dozen unused widgets and every colour format are compiled in (flash only).

### The digital level (`digital_level_view_type_1.c`, `digital_level_view_controller.c`)

The full-screen colour stays (owner). Within that design:

- Only the changed rows are redrawn (ADR-002 step 3). Near level about 6,800 px a flush, at up to 56 flushes a second.
- Each redrawn row is painted three times: the object's theme background, the tilt colour, then the foreground. Painting each row once in its final colour gives the same picture for up to a third of the fill work. Not measured in milliseconds.
- The display loop does not wait for the sensor: it checks every 20 ms for a report, and the sensor reports every 20 ms. The two clocks drift against each other, which adds up to 20 ms of varying delay, and a report is skipped whenever two land in one check (the queue keeps only the newest). Waiting on the report shows each one as it arrives, and also ends the polling while idle.

### Start-up (boot log of 2026-11-07, `rc_wifi_expire_idle_20261107_011310.log`)

- About 3.2 s from reset to the UI. About 1.07 s of it is inside the Wi-Fi driver's start (274 ms to 1,348 ms), before the display starts; why it takes that long is not known.
- The countdown timer's settings are written to flash on every boot although nothing changed; one harmless `gpio_install_isr_service ... already installed` error at every boot.

### Memory today (`stats`, 2026-11-08)

`free_int` 169,399 B, `largest_dma` 61,440 B (114,688 B in ADR-002's runs; a long-running unit with Wi-Fi on), `min_int` 102,992 B (a Kestrel pull's low-water mark, seen before). Record `largest_dma` in the next measurements, since the display's staging buffers depend on it.

## How we prove it without a meter

- **The unit counts its own sleep.** The light-sleep callbacks count the time asleep and what ended each sleep; ESP-IDF's power-management profiling (`CONFIG_PM_PROFILING`) adds the PM lock table. On battery with USB unplugged the unit sleeps; plugged back in, rc `sleep` shows the share of idle on battery spent asleep and the number of wakes by cause. Kept awake while USB power is in (a PM lock taken from the PMIC's VBUS reading), so the bench tools keep working.
- **The battery gauge as a rough check**: the charge before and after a few hours idle on battery, one run, not a soak.
- **Wake time from the log**: interrupt to first frame, stored and read after reconnecting.

## What we will do, and why

One change at a time, each built, flashed and checked on the unit before the next. The owner approved the plan on 2026-11-08 ("Let's start our experiment").

### Step 0: measure before changing (profiling build, nothing changed for the user)

Why: the later steps are judged against these numbers, and one of them decides step 2's sensor design. The owner kept the level's reports running partly because of the delay between a wake and the first rotation or acceleration report (2026-11-08): if the reports stop while the hub sleeps, that delay must be measured first.

- The wake path, timed on the unit (`WAKE_PROFILING_ENABLED`, one `wake_prof:` line per wake): the wake request, panel on, LVGL resumed, the first frame, the BNO085's wake command, and the first game rotation vector, linear acceleration and stability reports after it, with how many arrived in the first 100 ms (an always-on report delivers its sleep buffer at once).
- The same for each combination of the two reports always-on or not (`bno alwayson grv|la 0|1`, RAM only), with `scenario wake_profile` in `rc_run.py`.
- CPU per task while active and while idle (`top`, verification builds with run-time stats).

Measured 2026-11-08 ([progress](progress.md)): the panel shows a picture 6.5 ms after a wake request. With linear acceleration kept running (today), the first rotation report comes 23 ms after the sensor's wake; with only the stability detector running, 99 ms (linear acceleration 130 ms), so the level moves again at about 120 ms instead of 48 ms. Found on the way: while idle the LVGL task still wakes about every 2 ms (`lvgl_port_stop()` disables LVGL's timers, and LVGL then asks to be called again in 1 ms), 3.6% of a core; step 2 fixes it.

### Step 1: the PMIC's safety settings (built 2026-11-08, checked on the unit; hands-on checks left)

Why first: these are about the battery and the board's safety, not speed, and each is small.

1. Power off cleanly at the low level: the level-2 interrupt (5%) asks the PMIC service for the same power off as the menu; the warning level is shown as today.
2. DCDC1's under-voltage and the over-voltage cut-off back on (the defaults); the unused rails' protections stay off.
3. The hard cut-off from 2.6 V to 3.0 V, as a backstop behind step 1.1.
4. The interrupt: clear only the bits that were read; on each pass, if the pin is still low, service it.
5. TS measured properly: the 50 µA source set by us, converted with the datasheet's table, first as an input only (charging not gated), shown in `pmic` and on the Power settings page (`Board`). Once it reads right, it guards the charge with the datasheet's default windows.
6. Charging for the T9: termination 100 mA (about a tenth of 950 mAh) instead of 200 mA; for speed, the USB input limit raised from 500 mA to 1000 mA, so the running unit does not eat the charge current. The charge current itself stays at the settings page's 500 mA (about half the cell's capacity). These are settings-page defaults; a unit with saved settings keeps its own until reset.

Shown on the unit: the `pmic` readings unchanged except the temperature; the TS reading close to the room and rising under a warm finger; the power key, USB in and out still reported; power off and back on with the key, on battery; a charge from low ends at "done" near 4.2 V. The low-battery path is checked by forcing the interrupt handler from the remote control, not by draining the cell.

### Step 2: make idle sleep, and never while shooting (built 2026-11-08 except item 3, automatic light sleep)

1. The BNO085: linear acceleration not always-on; nothing but the stability detector runs while the hub sleeps.
2. Stop what has no work while idle: the level's display loop and the other sensor views' loops, the once-a-second log line, the PMIC's 2 s read (every 30 s while idle).
3. Automatic light sleep while idle (`light_sleep_enable = true` in `idle_pm_config`): GPIO wake-up on low for the touch (GPIO15), BNO085 (GPIO5), button (GPIO0) and PMIC (GPIO21) pins, their pulls kept through sleep (with `CONFIG_PM_SLP_DISABLE_GPIO`, set today, ESP-IDF may float pins while asleep; checked when built); edge interrupts as now once awake. Kept awake while a USB host is connected. As built: ESP-IDF does float every pin while asleep, so the reset, chip-select and I2C pins are held too; "connected" is USB power (VBUS, from the PMIC), because ESP-IDF's frame-pulse check let the chip sleep with the cable in and the USB link did not come back; mDNS is freed when the radio stops ([progress](progress.md), step 2c).
4. The panel to sleep-in after display off while idle, sleep-out on wake (50 ms by the datasheet, 80 ms in our init today): it saves milliamps for a wake still well under the time it takes to shoulder a rifle. Measured before and after; dropped if the wake gets noticeably slower.
5. The automatic power-off default from 1 h of idle to 5 h (an existing choice), once idle really sleeps.

Shown on the unit: the light-sleep share of idle near 100%; wakes by touch, button, power key and movement each work; the wake time from the first movement to the first frame, before and after; no wake without a cause; with the timer running and the unit held still past a 10 s idle timeout, the screen stays on; `state_change` passes, and one idle/wake cycle test of at most 10 minutes with no stall.

### Step 3: the level's drawing (3.1 built with step 2; measured while moving 2026-11-08, no regression; 3.2 set aside: drawing is 16-18% of the time while moving, the flush holds the lock most of the rest; see progress)

1. The display loop waits for each report instead of polling every 20 ms.
2. Each row painted once in its final colour.

Shown on the unit: render time and CPU per frame from `display_prof` against a measurement taken before the change; the owner's eye at all four rotations (no tearing, no stale rows).

### Step 4: start-up (set aside: the second is in the closed Wi-Fi library; see progress)

1. Find what holds the Wi-Fi start for a second, and start the display first, so the screen comes up a second sooner.
2. Stop the needless settings write and the harmless error at each boot.

Shown on the unit: boot to first frame from the log.

## Still open

- **The earlier BNO085 wake attempt**: what went wrong (woke at once, never woke, lost reports after the wake)? It tells step 2 where to look first.
- The T9's maximum charge current is not in the retailers' listings; 500 mA (about 0.5 C) is a safe rate for most cells of this size. A Vapcell datasheet would settle it.

## Set aside

Porting the ESP32-S3 assembly blending to LVGL 9.6, PSRAM above 80 MHz, a faster QSPI clock, internal draw buffers: only if a measurement shows rendering is the limit. Deep sleep instead of power off: power off is the lower floor (40 µA), and a wake from deep sleep is a reboot anyway. A dark level style: ruled out by the owner, the colour is the design.
