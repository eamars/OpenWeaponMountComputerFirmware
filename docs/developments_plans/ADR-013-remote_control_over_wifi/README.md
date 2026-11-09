# ADR-013: remote control over Wi-Fi

Status: **Built** 2026-11-09. Approved by the owner with no password on the network control. All six steps built and checked on the COM11 unit: `state_change` passes over USB and over Wi-Fi; snaps and a full screen capture over Wi-Fi match USB; no CPU or internal RAM cost found. Details, and two findings for ADR-014, in [progress](progress.md).

## What we want

The remote control and the device tests (`rc_run.py`, `ui_snaps.py`) work with a unit that isn't plugged into this PC: one running on its battery, or sitting on the turntable. The commands, replies, snaps and log lines are the same as over USB, so every scenario works either way.

This is the first half of the owner's idea "remote debug, with turntable integration". The turntable half needs a mount that is still being printed. It goes into the next ADR, together with the wake measurement it exists for (below).

## Owner's direction (2026-11-09)

- Ideas, ranked by value: the Dope tile's jump (fixed apart, as a bug), then the bench rig, then the slow recovery after sleep measured with it, then imperial units.
- The turntable is the OpenAutoTurret (`http://192.168.2.103:8080/`). Nothing is mounted on it yet; the owner is printing a mount. Its actuator drives a servo that presses the laser's switch. Its video is of no use to the rig.
- Until the mount is ready, work on whatever needs neither the turntable nor the owner's hands.

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

## Decision for the owner

**No password on the network control.** While "Over Wi-Fi" is on, anyone on the same network can send any command: tap, change tiles, reboot, start an update.
- Recommended: no password. The switch is off by default and stored, so the rig keeps working after a flash or reset. This is the same trust as the USB console, and at a match the Wi-Fi window has usually expired anyway.
- The alternative: a code shown on the unit's screen to pair a host. That makes every connection depend on someone reading the screen, which defeats a rig nobody touches.

## Next, not in this ADR

- **ADR-014, the turntable and the wake:**
  - drive the OpenAutoTurret's pitch (fixed steps within ±35°, encoder readback, its own IMU) with the unit mounted so pitch turns into cant;
  - measure the wake as the shooter gets it, on battery: from movement or touch to a frame, and to the level reading right again against the turntable;
  - fix what that shows.
- **Then imperial units** (MOA, yards). This needs a decision on the stored format: the card keeps holds in 0.1 mil, which cannot hold 0.25 MOA exactly.
