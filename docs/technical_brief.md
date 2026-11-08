# Technical brief

The hardware the firmware runs on: the parts, how they are wired, and how the motion sensor sits in the unit. The pin numbers are the ones in `main/board/board_cfg.h`; if the two ever disagree, the header is what the firmware uses.

## Parts

| Part | What it does |
|---|---|
| ESP32-S3 with 16 MB flash and 8 MB octal PSRAM | Runs the firmware, Wi-Fi and Bluetooth |
| DO0164FMST05 AMOLED panel, 280 × 456 pixels | The screen, driven over QSPI with a tearing-effect (TE) output; its corners are rounded (R2.30 mm, about 30 pixels) |
| FT3168 | The panel's touch controller |
| BNO085 (CEVA SH-2) | Motion sensor: gravity for the level, linear acceleration for shot detection, and the stability detector that wakes the unit when it is picked up |
| AXP2101 | Power manager: battery charger and gauge, the 3.3 V rail (DCDC1, the only rail the board uses), the power key, and a 10 kΩ NTC (TH1) on its TS input that reads the board's temperature |
| Buzzer | Beeps, driven by a PWM output |
| Button | One push button, used by the screens (it starts the timer) and to wake the unit |
| Battery | One 16340 Li-ion cell (Vapcell T9, 950 mAh, unprotected; 4.2 V full charge) |

## Wiring

**Buses**

| Bus | SDA | SCL | Devices (7-bit address) |
|---|---|---|---|
| I2C0 | GPIO40 | GPIO39 | AXP2101 (0x34), FT3168 (0x38) |
| I2C1 | GPIO7 | GPIO6 | BNO085 (0x4A) |

The panel is on SPI2 in QSPI mode:

| Signal | Pin |
|---|---|
| CS | GPIO10 |
| Clock | GPIO12 |
| Data 0 | GPIO13 |
| Data 1 | GPIO11 |
| Data 2 | GPIO9 |
| Data 3 | GPIO14 |
| Reset | GPIO17 |
| TE | GPIO18 |

The panel's visible area starts 20 pixels into the controller's frame horizontally.

**Other pins**

| Function | Pin |
|---|---|
| BNO085 interrupt | GPIO5 |
| BNO085 boot | GPIO1 |
| BNO085 reset | GPIO8 |
| AXP2101 interrupt | GPIO21 |
| Touch interrupt | GPIO15 |
| Touch reset | GPIO16 |
| Button (active low) | GPIO0 |
| Buzzer | GPIO47 |

The power key is wired to the AXP2101, which reports short and long presses through its interrupt.

## The sensor in the unit

- The BNO085 sits on the board opposite the screen, and its Z axis is along the screen's normal.
- The screen has four rotations, 0 to 3, in steps of 90°.
- Standing the unit on its rotation 0 edge (the bottom edge in rotation 0) puts gravity along the sensor's +Y axis.
- Cant is measured about the screen's normal, so the reading does not depend on which way the unit faces.
- Each unit is a little off this ideal (about a degree on the first unit measured). Sensor calibration measures the real direction once, on a flat surface, and stores it in the unit.

## Power

- The AXP2101 powers the whole board from DCDC1 at 3.3 V.
- USB powers the unit and charges the cell.
- TH1 sits on the PCB near the PMIC, so it reads the board, which runs warmer while charging, not the cell.
