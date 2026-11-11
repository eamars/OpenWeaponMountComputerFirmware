# ADR-016: charging from a USB supply that arrives below 4.7 V

Status: **Built** 2026-11-11, approved by the owner. Built and proven on the turret unit ([progress](progress.md)): with VSYS DPM at 4.1 V the cell charges on the PC port (0.43 A from USB, against 0.30 A at 4.3 V and none at 4.7 V); state_change passes.

## What we want

A unit on USB charges its cell at the set current, from an ordinary 5 V supply (a PC port, a phone charger), on the boards built today.

## Owner's direction (2026-11-11)

- A 5.1 V charger is not common: fix it in the firmware, by lowering the threshold.
- Units are hand-soldered; COM12's charging was taken as a hardware fault and parked. This ADR may explain it instead.

## What we know (measured 2026-11-11 on the turret unit, `owmc-8626470`)

- **The symptom:** on USB from the PC since the evening of 2026-11-10, the turret unit sat at 26% and 3.73 V, with the charger reporting constant-current charging (`chg=2`). COM12 did the same on 2026-11-10. ADR-009 recorded that "some units charged slowly".
- **What the charger reports** (`pmic regs`, built for this ADR, below):
  - every condition for charging holds: VBUS good, charging enabled (0x18 bit 1), battery present, state CC, current direction "charge";
  - nothing reports a limit: no thermal regulation (threshold 100 °C, die about 36 °C), no input current limit, not in input voltage DPM (set to 3.88 V);
  - reg 0x14, the linear charger's VSYS DPM (datasheet p.33, 4.1 V + N × 0.1 V), is 0x65: 4.7 V, the chip's default. The firmware never writes it;
  - VBUS reads 4.60 to 4.67 V and VSYS 4.59 to 4.67 V: always below 4.7 V.
- **Why that stops the charge:** the charger reduces its current to hold VSYS at the DPM level. From an input below 4.7 V, VSYS can never reach it, so the charger stays in CC with next to no current. No status bit shows this DPM. Not yet proven: proving it is this ADR's first step.
- **Why VBUS is low:** mostly the board's D2 Schottky diode on the USB input (hardware session, HDP-001). The v0.5 board replaces it with a P-MOSFET. Every board built so far has D2.
- **What VSYS feeds:** DCDC1, the one 3.3 V rail for the whole board. On battery VSYS follows the cell (3.73 V on this unit) and the unit runs, so 4.1 to 4.3 V on USB leaves it room.
- **The cell:** a Vapcell T9 (950 mAh, unprotected), charged to 4.20 V; 500 mA standard, 1000 mA quick charge (ADR-009). The charger feeds the cell from VSYS, so a DPM level below 4.2 V might stop it short of full: why 4.3 V comes first below.
- **The turret unit's own settings** (Settings, Advanced): USB current limit 2000 mA, charge current 1000 mA, charge voltage 4.2 V (set back from 4.4 V by the owner on 2026-11-11). The defaults are 1000 mA, 500 mA and 4.2 V.

## What building found

- The remote control's reading commands (`pmic`, `state`) do not count as use, so a unit left untouched goes idle after its idle timeout (5 minutes) and its radio stops, even on USB. The first 10-minute run at 4.1 V lost the unit after 6.5 minutes that way; the run was repeated with a `wake` every 30 s.
- The gauge's percentage rises much faster than the charge while charging (43% to 55% in 10 minutes at about 0.4 A from USB, more than the current allows): it follows the cell's voltage under charge. The cell voltage, the meter and VSYS are the evidence, not the percentage.

## What we will do

1. **A readout of the charger** (built on this branch, read only): `pmic regs` on the remote control prints the charger's registers raw and decoded; `pmic tdie on|off` switches the die temperature ADC channel. The PMIC monitor task does the reads, so it stays the only code talking to the chip.
2. **VSYS DPM lowered,** written at start-up with the other charge settings. Not a setting: a property of the board. First 4.3 V: low enough for a supply that arrives at 4.4 to 4.6 V, and above the cell's 4.2 V. If 4.3 V still holds the current back on the PC port, 4.1 V, the lowest, measured the same way.

   **Result: 4.1 V.** At 4.3 V the cell charged, but VSYS sat at 4.29 V, the DPM level, the whole time: the current was still held back. At 4.1 V the unit draws 0.43 A from USB against 0.30 A (the owner's inline meter, 4.98 V at the port), and VSYS sits at 4.05 to 4.09 V. It is a floor, not a ceiling: as the cell nears 4.2 V the current should taper, the drop across D2 shrink and VSYS rise, so the charger can still finish. Not watched: no run went to the end of a charge.
3. **Nothing else changes:** no stored settings, and the same charge current, voltage, termination and input limits. 4.35 V and 4.4 V stay on the settings page (not this ADR).

## How the unit shows it works

On the turret unit, on the same PC port and cable, over Wi-Fi:
- **Before:** the readings above (0x14 = 4.7 V, 3.73 V, 26%, no rise in a day).
- **After:** `pmic regs` shows 0x14 at 4.3 V. Within 10 minutes, the cell voltage rises and the gauge's percentage follows; VSYS sits at or above 4.3 V. A rise of a few tens of mV at once is the cell's resistance carrying current; the trend over 10 minutes is the charge.
- **No harm to the unit:** `scenario state_change` over Wi-Fi passes while it charges.
- **COM12,** if the owner brings it back: the same check, to see whether its "fault" was this.

## Not in this ADR

- Limiting the settings page's charge voltages to 4.2 V for the T9.
- A charge current readout: the AXP2101 has no current ADC.
- The v0.5 board's input (HDP-001, hardware repo).
