# ADR-001 progress

Execution record for [ADR-001](README.md). Times are NZDT.

## Stage 1 - focused contract (2026-11-05)

Tree at `c6f2c63` (branch `spiram_optimization`), before any ADR-001 code change.

| Check | Expected | Found |
|---|---|---|
| `rg -n "xTaskCreate\(" main components/bno08x/src` | 15 | 15 (14 calls + commented `mem_monitor_task` in `main/app.c:123`) |
| `rg -n "xTaskCreateWithCaps\(" ...` | 0 | 0 |
| `rg -n "vTaskDelete\(" ...` | 4 | 4 (`ota_mode.c:378`, `ota_mode.c:536`, `pmic_axp2101.cpp:451`, `wifi.c:114`) |
| `rg -n "vTaskDeleteWithCaps\(" ...` | 0 | 0 |
| `LCD_QSPI_MAX_TRANSFER_SIZE` | 4096 | 4096 |
| `DISPLAY_PROFILING_ENABLED` | - | 1 |

Result: pass.
