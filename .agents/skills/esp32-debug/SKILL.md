---
name: esp32-debug
description: Use when debugging ESP32 or ESP-IDF firmware on real hardware, especially serial monitor logs, flashing, device resets, COM/TTY ports, VS Code ESP-IDF setup, runtime instrumentation, PMIC/sensor/peripheral diagnosis, or distinguishing firmware configuration from board-level behavior.
---

# ESP32 Debug

Investigate ESP32/ESP-IDF behavior on attached hardware without guessing, hardcoding local paths, or leaving temporary firmware changes behind.

## Core Principle

Treat the device as a layered system: workspace configuration, toolchain, serial connection, boot logs, firmware state, peripheral registers, and hardware measurements. Gather evidence at each boundary before changing code or blaming hardware.

## Configuration Discovery

Do not hardcode serial ports, ESP-IDF paths, Python paths, or tools paths. Discover them from the workspace and environment.

1. Read workspace settings first:
   - `.vscode/settings.json`
   - `.vscode/*.json` if settings are split
   - documented project scripts or README only after workspace settings
2. Extract ESP-IDF keys when present:
   - `idf.espIdfPathWin`, `idf.espIdfPath`, `idf.currentSetup`
   - `idf.toolsPathWin`, `idf.toolsPath`
   - `idf.pythonInstallPath`
   - `idf.portWin`, `idf.port`
   - `idf.monitorPort`, `idf.flashType`
   - `idf.customExtraVars.IDF_TARGET`
3. Verify discovered paths before use.
4. If settings are missing, inspect `IDF_PATH`, `IDF_TOOLS_PATH`, `IDF_PYTHON_ENV_PATH`, `idf.py`, `esptool.py`, `cmake`, and `ninja`.

PowerShell pattern for JSON discovery:

```powershell
$settings = Get-Content -Raw ".vscode/settings.json" | ConvertFrom-Json
$idfPath = $settings.'idf.espIdfPathWin'
$toolsPath = $settings.'idf.toolsPathWin'
$port = $settings.'idf.portWin'
$monitorPort = $settings.'idf.monitorPort'
```

If CMake reports that the build directory was configured with a specific Python interpreter, match that environment before considering `fullclean`.

## Serial First

Capture current behavior before flashing.

1. Enumerate attached serial devices:

```powershell
Get-CimInstance Win32_SerialPort |
    Select-Object DeviceID,Name,Description,PNPDeviceID
```

Use the configured monitor port unless the user explicitly names a different device. Start with a passive capture at the expected baud rate, commonly `115200`, and avoid toggling DTR/RTS unless a reset is intentional.

PowerShell passive capture pattern:

```powershell
$port = New-Object System.IO.Ports.SerialPort $monitorPort,115200,'None',8,'One'
$port.ReadTimeout = 500
$port.DtrEnable = $false
$port.RtsEnable = $false
try {
    $port.Open()
    $deadline = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $deadline) {
        $chunk = $port.ReadExisting()
        if ($chunk.Length -gt 0) { Write-Output $chunk }
        Start-Sleep -Milliseconds 100
    }
} finally {
    if ($port.IsOpen) { $port.Close() }
    $port.Dispose()
}
```

Capture reset reason, app version, NVS/config loading, peripheral initialization, first runtime status loop, and any crash/watchdog backtrace.

## Build and Flash Safely

Build only after passive evidence shows the current firmware cannot answer the question.

1. Use the ESP-IDF paths discovered from workspace settings.
2. Set `IDF_TOOLS_PATH` and `IDF_PYTHON_ENV_PATH` only to match the configured build environment.
3. Run `idf.py build` before flashing.
4. Read the build output and stop on errors or diagnostic-relevant warnings.
5. Flash only the intended port.

```powershell
$env:IDF_TOOLS_PATH = $toolsPath
$env:IDF_PYTHON_ENV_PATH = $pythonEnvPath
. "$idfPath/export.ps1"
idf.py -p $port build flash
```

If `idf.py` is unavailable but build output gives an `esptool.py` command, use the generated command rather than inventing offsets.

## Temporary Instrumentation

Instrumentation should answer one narrow hypothesis and should be easy to remove.

Log raw and decoded evidence at the same timestamp:

- raw register values and decoded meaning
- persisted config value and live register readback
- state-machine state, not only "OK"
- voltage/current/status bits at the same timestamp
- hardware protection or derating flags
- comparable before/after fields for controlled changes

For ESP-IDF firmware, prefer existing logging idioms:

```c
ESP_LOGI(TAG, "diag: st1=0x%02x st2=0x%02x vbus=%dmV vbatt=%dmV",
    status1_reg,
    status2_reg,
    vbus_mv,
    vbatt_mv);
```

Keep temporary diagnostics out of final source unless the user asks to retain them. Before finishing:

1. Restore temporary code changes.
2. Rebuild restored firmware.
3. Reflash the device if diagnostics changed runtime behavior.
4. Verify `git diff` does not contain accidental diagnostic edits.

## Controlled Comparisons

Change one variable at a time. Keep cable, source, battery state, firmware shape, and measurement method constant. Examples: charge-current target, input-current limit, one peripheral enabled/disabled, display backlight high/low.

Capture the same fields before and after. If register readback changes but measured behavior does not, suspect hardware path, measurement location, protection circuitry, or a misunderstanding of what the chip can report.

## Interpreting Evidence

Separate these layers explicitly:

| Evidence | Meaning |
|---|---|
| Boot log configured value | Firmware attempted to write a setting |
| Live register readback | Device accepted or retained the setting |
| State bits or fault flags | Device-reported derating or stop reason |
| ADC telemetry | What the chip senses at its pins |
| External meter measurement | What the board path or load actually does |
| Unchanged behavior after controlled setting change | Bottleneck may be outside that firmware setting |

For power or charging issues, do not equate configured current with actual current. Check charger phase, input current limit, input-voltage regulation, thermal regulation, battery presence, precharge, CV taper, termination, safety timers, battery protector state, cable/source limits, and measurement point.

## Common Mistakes

- Reusing a remembered serial port or ESP-IDF install path from a previous session.
- Flashing before capturing the current boot/runtime logs.
- Treating a config menu value as proof of live register state.
- Changing several settings at once and losing the causal signal.
- Reading only decoded helper APIs when raw status bits are needed.
- Leaving diagnostic firmware flashed after forcing a behavior-changing setting.
- Reporting "fixed" when the evidence only shows "configured correctly."

## Report Format

When reporting results, keep the layers visible:

1. Device and toolchain discovered: port, target, ESP-IDF version, app version.
2. Current live evidence: logs, raw status, decoded state, measurements.
3. Controlled changes performed: exact variable changed and result.
4. Conclusion: firmware bug, bad configuration, runtime derating, external hardware/input issue, or unresolved.
5. Cleanup: whether source changes remain and whether the device was restored to normal firmware.
