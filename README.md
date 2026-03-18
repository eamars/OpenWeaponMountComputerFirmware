# Open Weapon Mounted Computer

An ESP32-S3 based embedded firmware for precision digital level applications, featuring real-time IMU sensor fusion and touchscreen interface.

## Overview

This firmware implements a digital level with integrated shot preparation tools. It uses the Bosch BNO085 inertial measurement unit for real-time sensor fusion, providing instant roll and pitch readings through an LVGL-based touchscreen interface.

## Key Features

- **Real-time IMU Sensor Fusion**: Bosch BNO085 providing instant roll/pitch readings via 9-axis sensor fusion
- **Two Digital Level Styles**: Type 1 (block-level) and Type 2 (alternative layout) interfaces
- **Recoil Detection**: Automatic linear acceleration monitoring that triggers countdown timer
- **Countdown Timer**: Auto-starts on recoil, manages shot preparation
- **DOPE Cards**: Maintains and auto-updates hold-off calculations for long-range shooting
- **Touchscreen GUI**: LVGL-based interface with tileview navigation and configuration menus

## System Architecture

The firmware uses a modular architecture with sensor, display, and UI layers:

```
┌─────────────────────────────────────────────────────┐
│         Touchscreen Display (LVGL + FT3168)          │
├─────────────────────────────────────────────────────┤
│     Digital Level View • Timer • DOPE Cards        │
├─────────────────────────────────────────────────────┤
│           Sensor Fusion & Processing                │
├─────────────────────────────────────────────────────┤
│   BNO085 IMU → Game Rotation Vector → Roll/Pitch    │
│   Linear Acceleration → Recoil Detection            │
├─────────────────────────────────────────────────────┤
│              Communication Interfaces               │
├─────────────────────────────────────────────────────┤
│     I²C: BNO085, AXP2101 PMIC, Touch Controller    │
│     SPI: LCD Display (QSPI, 280×456px)             │
└─────────────────────────────────────────────────────┘
                    ↓
           ESP32-S3 Core with PSRAM
                    ↓
        Power Management (AXP2101)
                    ↓
          Battery & Low Power Modes
```

### Sensor Integration Layer

- Game rotation vector reports for orientation (20ms update rate)
- Linear acceleration sensor for recoil detection
- Interrupt-driven data acquisition
- Stability classification integration

### Processing Engine

- Continuous coordinate calculation from sensor inputs
- Roll/pitch angle extraction and display
- Recoil event detection and timer management
- DOPÉ card value auto-updates during shots

### User Interface

- LVGL-based touchscreen interface with tileview navigation
- Multiple functional views with swipe interactions
- Real-time sensor data visualization
- Configuration menus with persistent storage

### Power Management

- AXP2101 PMIC with interrupt handling
- Three operational modes: active, idle, sleep
- Battery voltage monitoring and optimization
- Display power control based on system state

## Hardware Requirements

### Core Components
- ESP32-S3 microcontroller with PSRAM (minimum 8MB)
- Bosch BNO085 IMU sensor module via I²C
- AXP2101 PMIC sensor module via I²C  
- TFT LCD display (280×456 pixels) with touchscreen
- Rechargeable battery

### Pin Connections

#### I²C Interfaces
| Peripheral | Bus | SDA Pin | SCL Pin |
|-----------|-----|---------|---------|
| BNO085 IMU | I2C0 | GPIO40 | GPIO39 |
| AXP2101 PMIC | I2C0 | GPIO40 | GPIO39 |
| Touchscreen | I2C1 | GPIO7 | GPIO6 |

#### SPI Interfaces
| Peripheral | Host | MOSI Pin | MISO Pin | SCLK Pin | CS Pin |
|-----------|------|----------|----------|----------|--------|
| Display | SPI2 | GPIO13 | GPIO11 | GPIO12 | GPIO10 |

#### GPIO Connections
| Function | Pin |
|----------|-----|
| BNO085 Interrupt | GPIO5 │
| BNO085 Boot/Start | GPIO1 │
| BNO085 Reset | GPIO8 │
| PMIC Interrupt | GPIO21 │
| Touchscreen Interrupt | GPIO15 │
| Touchscreen Reset | GPIO16 │
| External Button | GPIO0 │
| Buzzer Output | GPIO47 │

#### Power Management
| Function | Pin |
|----------|-----|
| Display Reset | GPIO17 │
| Display TE/IRQ | GPIO18 │
| VSYS Output | 3.3V (from AXP2101) │

## Target Applications

This firmware serves two primary purposes:

**Digital Level Application:**
- Real-time sensor fusion providing instant roll/pitch corrections
- Shooters can verify level alignment within degrees during matches
- Eliminates need for external physical levels

**Competitive Shooting Training Tool:**
- Integrated countdown timer with auto-start on 'recoil'
- DOPÉ card management for hold-off calculations
- Touchscreen interface for rapid configuration changes

## Technology Stack

- **ESP-IDF**: Framework with ESP32-S3 support and PSRAM
- **LVGL**: Lightweight graphics library with touchscreen support
- **FreeRTOS**: Real-time operating system with task management
- **BNO085 Sensor Library**: SH2 sensor fusion stack
- **XPowersLib**: AXP2101 power management interface

## Project Structure

The firmware includes these major modules:

| Module | Purpose |
|-------|---------|
| Digital Level View | Primary sensor display with roll/pitch indicators |
| Countdown Timer | Shot preparation and timing |
| DOPE Cards | Maintains range/angle calculations |
| Sensor Calibration | Aligns sensor to physical level |
| Configuration | UI menus for system settings |

## License
GPLv3
