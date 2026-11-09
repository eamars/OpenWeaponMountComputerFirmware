# ADR-013: remote control over Wi-Fi

Status: **Open** 2026-11-09. Approved by the owner with no password on the network control. Steps 1 to 6 built and checked on the COM11 unit: `state_change` passes over USB and over Wi-Fi; snaps and a full screen capture over Wi-Fi match USB; no CPU or internal RAM cost found. Reopened by the owner's review of PR #13: step 7 (finding and telling units apart, two switches) is left. Details, and two findings for ADR-014, in [progress](progress.md).

## What we want

The remote control and the device tests (`rc_run.py`, `ui_snaps.py`) work with a unit that isn't plugged into this PC: one running on its battery, or sitting on the turntable. The commands, replies, snaps and log lines are the same as over USB, so every scenario works either way.

This is the first half of the owner's idea "remote debug, with turntable integration". The turntable half needs a mount that is still being printed. It goes into the next ADR, together with the wake measurement it exists for (below).

## Owner's direction (2026-11-09)

- Ideas, ranked by value: the Dope tile's jump (fixed apart, as a bug), then the bench rig, then the slow recovery after sleep measured with it, then imperial units.
- The turntable is the OpenAutoTurret, `http://rpi-turret.local:8080/` (by name, not address). Nothing is mounted on it yet; the owner is printing a mount. Its actuator drives a servo that presses the laser's switch. Its video is of no use to the rig.
- Until the mount is ready, work on whatever needs neither the turntable nor the owner's hands.

## Owner's review of PR #13 (2026-11-09)

- Use the turret's mDNS name, `rpi-turret.local`, not its address.
- Think through the whole life cycle: how does a user learn the unit's name or address to connect to it, and with several units, how do they pick the right one? Perhaps the Wi-Fi page shows the unit's mDNS name and IP address.
- Under Advanced, make "Remote control over USB" and "Remote control over Wi-Fi" separate settings.
- The COM11 unit is now mounted on the turret as its payload, with no USB cable: the firmware goes on by OTA, the OTA is driven by the remote control, and the turret can be moved to any angle.

## What we know (checked 2026-11-09)

- **The remote control is tied to the USB console.**
  - `rc_serial.c` reads command lines from the USB-Serial-JTAG driver and prints each reply with `printf`.
  - `snap` prints its picture to stdout as `@sd` data lines (`remote_control.c`).
  - The host's scenarios also judge by log lines: reset and abort markers, and the `wake_prof:` lines.
  - So a second transport has to carry three things: the replies, the snap data and the log.
- **The USB link has costs.**
  - Opening the port can reset the unit.
  - Snaps sometimes lose lines: today one lost 5 of its 52 data lines and had to be taken again.
- **On USB power the unit never light-sleeps.** `low_power_mode_set_usb_power()` holds a power lock while USB powers it (`low_power_mode.c`). Every bench test so far has been on USB, so none has seen the light-sleep wake a shooter gets on battery. That is where the owner's "slow recovery after sleep" can hide: ADR-009's 117 ms was measured from the `wake` command, on USB.
- **The radio stops whenever the unit idles,** and mDNS stops with it (`wifi_apply_radio()` in `wifi.c`). A network session therefore ends at every idle, and the `wake` command cannot reach an idle unit over Wi-Fi.
- **mDNS starts with each new address but gets no name** (`start_mdns_service()`, a TODO). Units are found by IP today (.150, .151 and .152 for the owner's three).
- **A unit has two unrelated names** after steps 1 to 6: `HUD-` and 7 digits for the Kestrel (from the Bluetooth MAC address, `kestrel_service.c`, shown when pairing), and `owmc-` and 6 hex digits on the network (from the Wi-Fi MAC address). With several units, nothing ties the one to the other, and the unit shows neither its network name nor its address.
- **The Wi-Fi page** shows a status line, the Wi-Fi switch, the set-up QR code and Forget network (`wifi_page.c`).
- **An update is checked once per boot** (`ota_mode.c`, `scripts/ota_host.py`), and there is no app rollback (`CONFIG_APP_ROLLBACK_ENABLE` off). A build that cannot reach the network again leaves a unit on the turret unreachable until it is plugged into USB.
- **The switch is stored.** `remote_control_config_t` holds one field, `serial_enable`, in its own NVS namespace, set by the Remote Control row on the Advanced page. If a field is appended, older saved settings still load through `load_config_grown`.

## What we will do

1. **Commands no longer print their own output.** `remote_control_execute()` gets a writer for the lines a command sends besides its reply (the snap data); the serial transport passes one that prints, as today. Nothing changes over USB.
   - Proof: a snap over USB gives the same picture as before, and `state_change` passes.
2. **A log stream.**
   - A hook on the ESP-IDF log (`esp_log_set_vprintf`) still prints to the console, and also copies each line into a buffer in PSRAM.
   - The logger never waits on the network: when the buffer is full, lines are dropped, counted, and the count is reported.
   - Lines written while no client is connected stay in the buffer until one connects, so what the unit logged during an idle is read after the wake.
3. **The network transport.**
   - A task listens on one TCP port (3333) while the unit has an address and the switch is on. It serves one client at a time; a new client replaces the old one.
   - Command lines go in. Replies, snap lines and the log come out, in the same line format as the console.
   - It stops with the radio and starts again with the next address.
   - mDNS gets a name, `owmc-` plus the last three bytes of the Wi-Fi MAC address, and announces the remote control as `_owmc-rc._tcp` while it runs.
4. **`idle <seconds>`:** idle, then wake by a timer after that time (the wake cause is "timer"). A remote scenario can then idle the unit and get it back. `idle` with no argument works as before.
5. **The switch.** An "Over Wi-Fi" row on the Advanced page, shown while Remote Control is on, and off by default. It is stored by appending a field to `remote_control_config_t`, so saved settings load as they are.
6. **The host.**
   - `rc_run.py` and `ui_snaps.py` take `--host <address or name.local>` in place of `--port`.
   - `rc_run.py find` lists the units announcing the remote control (zeroconf, already in `requirements.txt`).
   - After the unit idles, a session reconnects by itself.
   - `AGENTS.md` says how to use it.

7. **Finding a unit, and telling units apart** (the owner's review).
   - **One number per unit:** the network name uses the same 7 digits as the Kestrel HUD name, `owmc-8626470.local` beside `HUD-8626470`, from one helper in `common/`, so the two cannot drift. It comes from the chip, so it survives a reflash, an update and a settings reset.
   - **The Wi-Fi page shows "Name" and "Address"** while the unit is connected (the address exists only then). That is where a user reads what to connect to.
   - **`rc_run.py find` shows each unit's firmware version** beside its name and address, from a field in the mDNS announcement.
   - **Two switches on Advanced, each on its own:** "Remote control over USB" (the old switch, its stored value kept, on by default) and "Remote control over Wi-Fi" (off by default, no longer needing USB on). The stored layout does not change.
   - Docs and tools use names, not addresses: a router can hand out a new address, but the name stays.

## How the unit shows it works

On COM11, still powered by USB:
- **Over USB, nothing changed:** `state_change` passes, and a snap gives the same picture as before step 1.
- **Over Wi-Fi:**
  - `state_change` passes, its idles using `idle 20`;
  - 20 snaps in a row come back whole, each timed against USB;
  - a `ui_snaps.py` capture over Wi-Fi matches one over USB, apart from live values.
- **The cost:**
  - internal RAM free and the largest DMA block (`stats`), with the switch off and on;
  - CPU while a client is connected and the log is streaming (`top`).
- **Off means off:** with the switch off, nothing answers on port 3333.

Each test takes under 10 minutes. A run on battery isn't needed here, because the transport is the same either way. The first battery run belongs to the wake measurement.

Step 7, on the unit on the turret, with no cable:
- the build goes on by OTA: `ota_host.py` serves it, `reboot` over Wi-Fi, then Install and Restart are tapped through the remote control;
- after the restart, `find` lists the unit under its new name with the new version, and `state_change` passes over Wi-Fi;
- snaps of the Wi-Fi page (name and address) and the Advanced page (two switches), in the unit's rotation and one other.

## Decision for the owner

**No password on the network control.** While "Over Wi-Fi" is on, anyone on the same network can send any command: tap, change tiles, reboot, start an update.
- Recommended: no password. The switch is off by default and stored, so the rig keeps working after a flash or reset. This is the same trust as the USB console, and at a match the Wi-Fi window has usually expired anyway.
- The alternative: a code shown on the unit's screen to pair a host. That makes every connection depend on someone reading the screen, which defeats a rig nobody touches.

## Next, not in this ADR

- **ADR-014, the turntable and the wake:**
  - drive the OpenAutoTurret's pitch (steps of 0.5°, 1° or 5° within ±35°, encoder readback, its own IMU). Checked 2026-11-09 with the unit on it: turret pitch turns into the unit's cant (+4.90° of encoder moved cant by 4.36°, and back by 4.78°: a first hint about accuracy or settling, for that ADR);
  - measure the wake as the shooter gets it, on battery: from movement or touch to a frame, and to the level reading right again against the turntable;
  - fix what that shows.
- **Then imperial units** (MOA, yards). This needs a decision on the stored format: the card keeps holds in 0.1 mil, which cannot hold 0.25 MOA exactly.
