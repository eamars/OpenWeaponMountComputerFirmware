# ADR-016 progress

The turret unit (`owmc-8626470`), 2026-11-11, on USB from the PC through the owner's inline meter, with the same cable throughout. Read over Wi-Fi (`rc_run.py --host`); the unit was updated over the air (`ota_host.py`), as no USB data port reached it. Its settings: USB current limit 2000 mA, charge current 1000 mA, charge voltage 4.2 V (set back from 4.4 V by the owner that evening).

## Built

- `pmic_axp2101.cpp`: VSYS DPM (reg 0x14) written at start-up with `setLinearChargerVsysDpm`, before the settings page's charge settings: 4.1 V. Logged at start-up ("Set linear charger VSYS DPM to"); the console was not read, as no cable reached it: `pmic regs` showed the register.
- The readout: `pmic regs` and `pmic tdie on|off` on the remote control (`remote_control.c`). The PMIC monitor task reads the registers on request (`pmic_read_charger_regs`) and switches the die temperature channel (`pmic_request_die_temp_adc`), so it stays the only code talking to the chip. `pmic regs` writes nothing.
- Build: no warning from our code.

## Measured

Each row: the remote control's `pmic` every 30 s. VBUS and VSYS are the PMIC's ADC; the meter is the owner's, at the USB port.

| VSYS DPM (0x14) | Firmware | Meter | VBUS / VSYS | Cell | Gauge | Board (TH1) |
|---|---|---|---|---|---|---|
| 4.7 V (0x65, the chip's default) | v1.4.0 | — | 4.59-4.67 / 4.59-4.68 V | 3.73 V, no rise since the evening before | 26%, no rise | 35.8 °C |
| 4.3 V (0x25) | `v1.4.0-3-g10d267b` | 0.30 A, 1.5 W | 4.32-4.33 / 4.29-4.31 V | 3.730 V before; 3.781 V at the first reading; 3.834 V after 10 min | 26 → 29% | 38.8 → 43.0 °C |
| 4.1 V (0x05) | `v1.4.0-4-gca7daef` | 0.429 A at 4.980 V (2.14 W) | 4.14-4.15 / 4.05-4.09 V | 3.954 → 4.014 V in 10 min (screen on) | 43 → 55% | 43.5-44.0 °C |

- At 4.7 V the charger reported CC (`01=32`: charging, CC) with every condition for charging met and no limit flagged (`00=38`: no thermal regulation, no input current limit; not in VINDPM).
- At 4.3 V and at 4.1 V, VSYS sat on the DPM level throughout: the charger was holding the current there in both, and lowering it raised the current from USB by 0.13 A.
- The gauge's rise at 4.1 V (12% in 10 minutes, about 114 mAh) is more than 2.14 W from USB can deliver: the percentage follows the cell's voltage under charge.
- The first 4.1 V run lost the unit after 6.5 minutes (32 → 37%, 3.878 → 3.922 V): it went idle, as reading commands do not count as use. Repeated with a `wake` every 30 s (the row above).
- `scenario state_change` over Wi-Fi with `--min-largest-dma 70000`, at 4.1 V while charging: PASS. Free internal 162,323 B, largest DMA block 86,016 B (ADR-015: 162,563 and 86,016).

## Not measured

- The end of a charge (constant voltage and termination at 4.2 V with VSYS DPM at 4.1 V).
- The charge current itself: the AXP2101 has no current ADC.
- COM12: parked by the owner; the same check would show whether its charging fault was this.

## After the release: USB good switching on a PC port (2026-11-11)

The turret unit (`pmic dump`, read over USB on the PC's port for COM11; cell 4.07-4.08 V):
- USB current limit 2000 mA, then 500 mA: reg 0x00 bit 5 (VBUS good) switched off and on every 1-2 s. The charger went between trickle (0x01 = 30) and discharge (0x01 = 55); the ADC read VBUS and VSYS at 4.55-4.65 V throughout.
- 100 mA, after a reboot: steady, 0x00 = 39 (VBUS good, input current limit), 0x01 = 32 (CC), VBUS 4.53 V, VSYS 4.10 V.
- Tried and dropped: VSYS DPM following the cell (VBAT + 0.2 V, at least 4.1 V), for the charger's VBUS > VBAT + 150 mV condition (datasheet p.11, p.23). At 4.3 V with the cell at 4.07 V, 500 mA, the same port: it still switched (8 of 10 samples not good). That condition is not the cause.
- Through the owner's inline meter (the ADR-016 runs), the charger held CC with no switching.
- COM12 (v1.4.1, 192.168.2.161), 2026-11-12, through the inline meter on another supply, USB limit 500 mA, cell 4.105-4.122 V: 23 of 23 readings over 90 s VBUS good, CC (0x00 = 38, 0x01 = 32); VBUS 4.27 V, VSYS 4.21-4.23 V; the meter steady at 0.362 A. VBUS was about 0.15 V above the cell, and the charger held. The switching follows the PC's port or its cable, not the unit or the firmware; the port itself was not measured.
