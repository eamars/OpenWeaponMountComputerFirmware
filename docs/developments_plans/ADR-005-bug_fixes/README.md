# ADR-005: review every module, fix the known bugs, then decouple the services from the UI

Status: **Open.** Approved 2026-11-06 ("Approve, start"). Step 1 (review) done 2026-11-06. Step 2 (bug fixes) approved 2026-11-06, done 2026-11-07. Step 3 (decoupling) done 2026-11-07: Wi-Fi, PMIC and low power changed; BNO085 needed no change.

## What we want

Every module mapped to its known bugs and performance issues, from a review of each subsystem on its own and of how the subsystems work together. Then the bugs on that map fixed at their cause, each proven on the device. Performance issues go on the map as a record; none is addressed in this ADR.

## Owner's direction (2026-11-06)

1. A new ADR for bug fixes, started after ADR-003.
2. No performance optimisation in this round. Only bugs are fixed.
3. The ADR starts with each module mapped to its known bugs and performance issues; only the bugs are addressed in ADR-005.
4. At system level, LVGL, the BNO085, Wi-Fi, the PMIC and the other peripherals are reviewed one by one, so the system-level interactions are covered too.
5. Bug fixes are functional changes on today's dependencies (ESP-IDF v6.1 and the managed components as locked by ADR-003). No dependency update inside this ADR.
6. Review not just the code but the life cycle of each submodule: the BNO085 from init to sleep, Wi-Fi from provisioning to auto shut-off, the LVGL locks and multi-thread rendering.
7. Most of the code is hand-written: check styling and consistency too.
8. Decouple the code from the UI: each module initialised without depending on other modules, along MVC lines if that is the right approach. Folded into this ADR as step 3, after the bug fixes (owner, 2026-11-06). The owner accepted the service, event and UI-bridge design proposed for it.
9. The second OTA source is a placeholder, not a bug: OTA is designed to fetch from a central server that is not enabled yet, with a local server overriding it when one is available.

## Step 1: the review and the module map

One review per subsystem, done as a walk through each life cycle rather than file by file:

- **LVGL and the display:** start-up (port task, lock, display, touch and button input, the two software draw units, `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2`), every task that takes the LVGL lock and for how long, what the draw units touch while the LVGL task renders, timers, stop/resume across idle, the LVGL watchdog user, and the panel's on/off, brightness and TE input.
- **BNO085:** power-on and reset, detection (where the first-boot abort lives), report configuration, the sensor poller, calibration, sleep on idle, wake, and recovery from "BNO085 Reset Unexpectly".
- **Wi-Fi and network:** init, provisioned or not, SoftAP provisioning (PoP, QR, timeout), credentials, connect and retry, IP, mDNS, OTA polling and download, the OpenTrickler client, the expiry watchdog and auto shut-off, stop/start across idle, the user's on/off switch and provisioning reset.
- **PMIC:** init, interrupts, the monitor task, charge and battery reporting, power-off and shut-down paths.
- **Other peripherals:** FT3168 touch, the external button (GPIO0), buzzer (LEDC), both I2C buses and who shares them, the USB Serial/JTAG console and remote control, NVS settings (load, CRC, defaults, save, reset).
- **System:** `app_main` start-up order, active/idle/wake and power management, crash reporting and memory diagnostics, every task with its priority, core and stack, and the task watchdog.

Each walk checks:
- every piece of state through its whole life: created, used, released, and what happens across idle/wake and a restart;
- every place it meets another subsystem: the LVGL lock, shared variables, event groups, task priorities, the watchdog, power management and the idle/wake order;
- on the device, only where reading the code cannot settle a question (short scenarios, cycle tests of at most 10 minutes, nothing that blanks or resets the screen while the owner is at the unit).

Styling and consistency are checked against the project's own rules: `.agents/skills/c-style` for all C and C++, and `.agents/skills/lvgl-layout` and `lvgl-widget` for the views. Also checked: naming, error handling, logging and comments that differ from module to module. Third-party code (`components/XPowersLib`, the `sh2` library) is out of scope for style.

| Review | Modules |
|---|---|
| LVGL and the display | `lvgl_display.c`, `display_flush.c`, `main_tileview.c`, every view and settings page (`*_view.c`, `*_config.c`, `config_view.c`, `countdown_timer*.c`), the SH8601 panel setup in `bsp.c` |
| BNO085 | `components/bno08x` (with the `sh2` library), `digital_level_view_controller.c`, `sensor_config.c`, `sensor_calibration_view.c`, `acceleration_analysis_view.c` |
| Wi-Fi and network | `wifi.c`, `wifi_config.c`, `wifi_provision.c`, `ota_mode.c`, `opentrickler_remote_controller_view.c`, mDNS |
| PMIC | `pmic_axp2101.cpp`, `pmic_config.c`, `components/XPowersLib` |
| Other peripherals | FT3168 touch (`components/esp_lcd_touch_ft3168`), external button (GPIO0), buzzer (`buzzer.c`, LEDC), the two I2C buses, the display's TE input, the USB Serial/JTAG console (`components/esp_driver_usb_serial_jtag`, `rc_serial.c`, `remote_control.c`), NVS configuration storage (`common.c`) |
| System | `app.c` start-up order, `low_power_mode.c` (idle/wake, power management), `crash_report.c`, `mem_diag.c`, `circular_buffer.c`, the task list, stacks and the task watchdog |

The 1.47" display and its touch controller (`esp_lcd_jd9853`, `esp_lcd_touch_axs5106`) belong to the dev kit, not to this unit. They are checked only so far as they still build.

The map is a table per module: known bugs (fixed in step 2), performance issues (recorded only) and style and consistency findings. Each entry says how it is known: seen on the device, read in a log, or found in the code with its effect worked out. The map goes to the owner, who approves the bug list for step 2.

### Step 1 result: the module map (2026-11-06)

The full notes, with file and line references and how each finding is known, are in [progress](progress.md). "Device" means seen on the unit; "code" means found by reading, with the effect worked out; "confirm" means a short device check comes before its fix.

**Bugs (to fix in step 2, once approved)**

| ID | Module | Bug | Known by |
|---|---|---|---|
| B1 | Display flush | A strip transfer now and then never completes; the LVGL watchdog reboots (about 9 s outage). Cause unknown; the core dump lacks the SPI driver's state | device |
| B2 | Status bar (`config_view.c`) | Wi-Fi and battery icons are rewritten behind LVGL's back (no redraw, no re-measure); the Wi-Fi path also skips the lock. **Moved to step 3**: the UI bridge replaces this code | code, confirm |
| B3 | Low power / touch | Touch never counts as activity and never wakes the unit from idle (the wrapper tests the data before the read fills it) | code, confirm |
| B4 | Low power / LVGL watchdog | A stuck LVGL lock during idle is not caught until the next wake, which then reboots | code |
| B5 | BNO085 driver | The `sh2` library is called from five or more tasks with no lock | code |
| B6 | BNO085 driver | ~~Reports re-enabled from inside the `sh2` callback after an unexpected reset may fail~~. **Checked, not a bug**: three forced resets on the device, every report restored and confirmed | device |
| B7 | BNO085 users | Every BNO085 call is wrapped in `ESP_ERROR_CHECK`, so any sensor hiccup reboots the unit; includes the first-boot abort after a reset (`app.c:194`) | device (abort), code |
| B8 | Wi-Fi | No reconnect after a disconnect: the handler calls `esp_wifi_start()` on a started stack, which does nothing | code, confirm |
| B9 | Wi-Fi | Provisioning name and hostname built from an uninitialised MAC (`esp_wifi_get_mac` before `esp_wifi_init`) | code, confirm |
| B10 | Wi-Fi | "Reset provisioning" aborts the unit once the Wi-Fi expiry has de-initialised Wi-Fi | code |
| B11 | Wi-Fi provisioning | The received Wi-Fi password is printed to the console | code |
| B12 | Wi-Fi | Expiry sets "provision expired" also when provisioned (should be "connect expired"); an always-true condition (`or` for `and`) | code (minor) |
| B13 | OTA | ~~The second source can never resolve~~. **Not a bug** (owner): a placeholder for the central server, not enabled yet | device |
| B14 | OTA | A failed update leaves the OTA handle open; progress divides by -1 when the size is unknown; a version without `v` is compared as garbage | code (minor) |
| B15 | PMIC settings | Charge current, charge voltage and USB current limit only take effect after a reboot, and the page does not say so | code |
| B16 | Touch driver (FT3168) | The reset uses pin 0: it drives GPIO0 (the button) low for 10 ms and never resets the touch controller. Cause of the v6.1 GPIO0 warning | code |
| B17 | Console | `stats` replies lost now and then (2 of 40 in one run, 0 of 21 in the last). Cause unknown | device |
| B18 | Buzzer | A beep requested while another is pending is dropped silently | code (minor) |
| B19 | Boot log | `GPIO isr service already installed` (installed twice) and `task_wdt: task not found` for 7 tasks that were never added | device |
| B20 | Crash report | After a watchdog reset it names the interrupted task (`IDLE0`), not the hung one (`lvgl`) | device (minor) |
| B21 | Start-up | About 32 KB of internal RAM borrowed during v6.1 start-up (cause unknown; may be expected) | device |
| B22 | Test tooling | Device-test logs live in `build/`, which fullclean deletes (ADR-002's raw logs were lost) | device |
| B23 | Test tooling | `scenario watchdog` does not wake the unit first | device |
| B24 | `app.c` | ~~The PMIC context allocation is not checked for NULL~~. **Checked, not a bug**: `axp2101_init` rejects a NULL context and `app.c` aborts on it, as for the BNO085 | code (minor) |
| B25 | Digital level | The roll reading is 180 degrees off the owner's expectation: standing on its edge with the screen facing right, the level should read 0 (owner, 2026-11-07; geometry still to work out) | owner |

**Performance issues (recorded only, not addressed here)**

- The low-power task logs every second at INFO; the stability task, buzzer and manifest fetch also log per event. All of it shares the console with the remote control.
- Every wake runs `mdns_free()` and `mdns_init()` (memory stayed steady over 54 cycles).
- The PMIC task reads six values over I2C every 2 s, also while idle.
- The display pipeline's own record is in ADR-002.

**Risks (not shown to fail)**

OTA over plain HTTP with no image signing; OpenTrickler discovered only once per boot; power-off with USB connected; 31 LVGL lock calls wait forever; the low-power event group and tasks created inside a view; mDNS left running and `esp_netif_deinit()` called after the Wi-Fi expiry; `wifi_config_set_enable` (and the remote control's `wifi on`) ignoring the expiry and idle state; the FT3168 init's infinite I2C timeout; a corrupted BNO085 packet could index past the report table; the core dump routine had 436 B of its 1,792 B stack left.

**Style and consistency (for the later pass)**

136 `ESP_ERROR_CHECK` in `main/`, many in runtime paths; 77 `extern` declarations in `.c` files; one-space-indented function definitions, a tab-indented one, `%d` for `uint32_t`; log tags in mixed case styles; about 95 lines of commented-out calls; typos and stale comments per module; dead modules in the build (the point-of-aim, acceleration-analysis and OpenTrickler tiles are commented out; `enter_sleep_mode`, `mem_monitor_task`, `axp2101_deinit`).

**Checked and not a bug:** `axp2101_deinit`'s missing free (never called); the level-style switch's "TODO: Add lock" (runs in the LVGL task); `PMU.disableTSPinMeasure()` (on purpose); the touch and button wake path while LVGL's timers are stopped (the port task reads them directly); the countdown timer's locking; panel commands from LVGL events; NVS settings recovery.

## Step 2: fix the bugs on the map

- Only the bugs the owner approves from the map; performance issues stay on the map.
- One bug per change, each built, flashed and proven on the device before the next. The proof shows the failure before the fix where it can be reproduced, and shows it gone after.
- Tooling first (B22, B23), so the evidence for the rest is kept.
- Order after that (owner approved 2026-11-06): B16, B3, B5 then B7 then B6, B8, B10, B9, B11, B15, B4, B14, B12, B18, B20, B24, and B19 as one change. The open causes (B1, B17, B21) are investigated alongside; each fix is proposed once its cause is known. B2 moved to step 3; B13 is not a bug. The BNO085 first-boot abort is in, as part of B7 (owner, 2026-11-06).
- The cause before the fix. Where the cause is unknown, the first step is to find it, and the fix is proposed once it is known.
- The boot-log noise as one change.

## Step 3: decouple the services from the UI

Why: the review found the subsystems coupled through the UI and through each other, and their races handled by ordering rules and guards instead of by structure (the race list and the coupling are in [progress](progress.md), Wi-Fi section). Wi-Fi calls the status bar and settings page directly and must start after them; Wi-Fi and low power depend on each other; services start inside views' create functions; event groups are created lazily so any start-up order survives; the LVGL lock is taken in about thirty places, each its own way.

Approach (owner accepted the proposal, 2026-11-06). Not classic MVC, which adds a controller layer without solving the actual problem (state and threads crossing module boundaries with no owner), but:

1. **A service owns its state and one task.** Wi-Fi, PMIC, BNO085 and low power each sit behind a small API and never call LVGL. Requests (for example "enable Wi-Fi") go to the service's own task, so `esp_wifi_*` is never called with the LVGL lock held.
2. **Services publish state changes as events** on the ESP-IDF event loop. Low power publishes idle and wake; Wi-Fi subscribes and stops and starts itself, which breaks the Wi-Fi / low-power cycle.
3. **One UI bridge** turns service events into widget updates under the LVGL lock: one place for locking. LVGL 9's observer module (`lv_subject`) is the candidate for binding widgets to those values; it is in LVGL 9.6 but not yet tried on this unit, so it is tried on the pilot before it is used further.
4. **Any start-up order.** Each service starts without the UI; the UI reads each service's current state when it is created. That removes the lazy creation, the "create the configuration view last" FIXME and the readiness checks.

Wi-Fi is the pilot (with B2 and the lock ordering folded in), then PMIC, BNO085 and low power, one service per change, each proven on the device. No new features and no performance work; the behaviour seen by the user stays the same.

## For the owner to decide

1. The BNO085 first-boot abort: in this round, as part of B7 (owner, 2026-11-06); the likely cause is in the progress notes.
2. The core dump stack margin: left as it is (owner agreed, 2026-11-07). Every recorded dump logs "used 1440 bytes on stack, 436 bytes left free"; the use depends on how the dump is written (flash driver, CRC32 checksum), not on what the app was doing, and an overflow would only lose that one crash's report (ESP-IDF's watchpoint at the stack end stops the dump). Raising it costs internal RAM for good. Look again only if the dump's write path changes (SHA-256 checksum, a new ESP-IDF). Flash encryption stays off: this is an open source project, so there is nothing to encrypt (owner, 2026-11-07).
3. Style findings: recommended as a separate pass after the bug fixes, module by module, so each bug fix stays a small change that is easy to check. Kept out of the bug-fix changes either way. In this ADR as step 3, or a later ADR?
4. Step 3, BNO085: no service change (owner accepted, 2026-11-07). The driver already meets the rules: one task (the sensor poller), its own state and lock (B5), no LVGL calls, started at boot without the UI. What remains are the views' reader tasks (level, point of aim, acceleration analysis, calibration), which are UI code reading the sensor and take the LVGL lock their own way. Moving them behind the UI bridge would give one place for locking, but it rebuilds the level's 50 Hz drawing path that ADR-002 tuned, which is performance work this ADR excludes; the cost is four views reworked and the frame rate measured again.
5. Step 3, low power's display calls: kept in low power (owner accepted, 2026-11-07). Low power is the display's power switch (LVGL stop and resume, panel off, watchdog pause, the touch wake wrapper); those calls are display control, not widget updates, and moving them into `lvgl_display.c` would rename them without removing a cycle or a race, while the order inside idle entry and exit (idle flag, BNO085 sleep, Wi-Fi) is what the idle/wake tests prove. The plan's "low power publishes idle and wake" events are not built either: no service would subscribe (Wi-Fi takes a synchronous request, see the pilot notes; the BNO085 is told directly), and an event nobody reads is code for nothing.
