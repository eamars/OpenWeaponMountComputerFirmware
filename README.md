# Open Weapon Mount Computer

The Open Weapon Mount Computer (OWMC) is a small open-source computer that mounts on a rifle. It is built for precision rifle shooting such as PRS matches: a digital level you read at a glance, a stage timer and a dope card, all on a bright touch screen that works in portrait or landscape.

This repository holds its firmware.

## What it does

**Digital level**
- Shows the rifle's cant, and turns the whole screen the level colour when the rifle is level.
- Three styles to pick from:
  - Level;
  - Block;
  - Chevrons: moving arrows that point the way to turn the rifle, faster the more it cants.
- A rifle zero sets the level to how the rifle sits.

**Stage timer**
- A countdown started from the button at the beep.
- Can also start on the first shot, when that is switched on.
- Stays awake while it runs, so the screen never goes dark on the line.

**Dope card**
- A card of 10, 12 or 15 targets, with elevation and windage for each.
- The current target shows on the level view.
- With a Kestrel weather meter, a single Pull over Bluetooth fills the card from the Kestrel's target card.

**Sensor calibration**
- A guided routine run once per unit on any flat, firm surface: no level table or compass needed.
- Stand the unit on its edge, turn it around, then turn it back. Beeps tell you when each placement is taken.

**Power**
- Between stages the unit sleeps.
- It wakes on a touch, the button or picking it up.
- It switches itself off after a long idle and before the battery runs flat.

**Updates**
- New firmware installs over Wi-Fi from a computer on the same network, or over USB.

## Getting the firmware

Each release on the [releases page](https://github.com/eamars/OpenWeaponMountComputerFirmware/releases) has:
- the firmware;
- an update package for installing over Wi-Fi;
- the files for flashing over USB;
- notes saying what changed and anything to do after updating.

The notes are also kept in [docs/releases](docs/releases).

## The hardware

The unit is built around an ESP32-S3, an AMOLED touch screen, a BNO085 motion sensor and an AXP2101 power manager, and runs on a single 16340 cell. The [technical brief](docs/technical_brief.md) describes the parts, how they are wired and how the sensor sits in the unit.

## Working on the firmware

The firmware is an ESP-IDF project. Clone it with its submodules (`git clone --recursive`), then build and flash it with `idf.py build` and `idf.py -p <port> flash`. The ESP-IDF version it is built with is the one the [build workflow](.github/workflows/firmware.yml) uses.

Before changing it, read [AGENTS.md](AGENTS.md): where code goes, the rules it keeps, and how a change is proven on a unit.

How the firmware got to where it is, and the reasons behind each design, are in the architecture decision records under [docs/developments_plans](docs/developments_plans/README.md).

Tools:
- `scripts/ota_host.py` serves an update to units on the local network.
- `tools/remote_control/rc_run.py` drives a unit over USB for tests on the device.

## License

GPLv3. See [LICENSE](LICENSE).
