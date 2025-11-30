# Zephyr RTOS Driver Development Tutorial

A comprehensive guide to building device drivers in Zephyr RTOS with out-of-tree modules and applications.

## Table of Contents

1. [Introduction](#introduction)
2. [Learning Roadmap](#learning-roadmap)
3. [Prerequisites](#prerequisites)
4. [Project Structure](#project-structure)
5. [Quick Start](#quick-start)
6. [Tutorials](#tutorials)

---

## Introduction

This tutorial repository provides a complete guide to developing drivers in Zephyr RTOS. It covers:

- **Out-of-tree module and application development**
- **Peripheral drivers**: UART (full capability), SPI (with DMA & interrupts)
- **Sensor drivers**: MPU6050, uBlox NEO-M8N GPS, Ultrasonic sensor
- **Best practices** for driver architecture, Kconfig, Device Tree, and API design

---

## Learning Roadmap

### Phase 1: Foundations (Start Here)
```
📁 docs/
├── 01_zephyr_basics.md          # Zephyr fundamentals
├── 02_device_model.md           # Device model & driver architecture
├── 03_devicetree_guide.md       # Device Tree bindings
└── 04_kconfig_guide.md          # Kconfig system
```

### Phase 2: Peripheral Drivers
```
📁 drivers/
├── uart_advanced/               # UART with interrupts, DMA, async
└── spi_advanced/                # SPI with DMA, chip select, modes
```

### Phase 3: Sensor Drivers
```
📁 drivers/
├── mpu6050/                     # I2C accelerometer/gyroscope
├── ublox_neo_m8n/               # UART GPS module
└── ultrasonic/                  # GPIO-based distance sensor
```

### Phase 4: Integration
```
📁 apps/
├── sensor_hub/                  # Multi-sensor application
└── data_logger/                 # Complete data logging example
```

---

## Prerequisites

- Zephyr SDK installed (v0.16.0+)
- West tool configured
- Basic C programming knowledge
- Understanding of embedded systems concepts

### Environment Setup

```bash
# Set Zephyr environment
source ~/zephyrproject/zephyr/zephyr-env.sh

# Verify installation
west --version
```

---

## Project Structure

```
zephyr_tutorial/
├── README.md                    # This file
├── docs/                        # Documentation & guides
│   ├── 01_zephyr_basics.md
│   ├── 02_device_model.md
│   ├── 03_devicetree_guide.md
│   ├── 04_kconfig_guide.md
│   ├── 05_driver_development.md
│   └── 06_testing_debugging.md
│
├── modules/                     # Out-of-tree modules
│   └── custom_drivers/
│       ├── CMakeLists.txt
│       ├── Kconfig
│       ├── zephyr/
│       │   └── module.yml
│       └── drivers/
│           ├── uart_advanced/
│           ├── spi_advanced/
│           ├── mpu6050/
│           ├── ublox_neo_m8n/
│           └── ultrasonic/
│
├── apps/                        # Example applications
│   ├── uart_example/
│   ├── spi_example/
│   ├── mpu6050_example/
│   ├── gps_example/
│   └── ultrasonic_example/
│
└── boards/                      # Custom board definitions (optional)
    └── overlays/
```

---

## Quick Start

### Building an Example Application

```bash
# Navigate to an example application
cd apps/uart_example

# Build for your target board
west build -b nrf52840dk/nrf52840 -- -DEXTRA_ZEPHYR_MODULES=/path/to/zephyr_tutorial/modules/custom_drivers

# Flash to device
west flash
```

### Using the Out-of-Tree Module

Add to your application's `CMakeLists.txt`:

```cmake
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/custom_drivers)
```

Or via west build:

```bash
west build -b <board> -- -DEXTRA_ZEPHYR_MODULES=/path/to/modules/custom_drivers
```

---

## Tutorials

| Tutorial | Description | Difficulty |
|----------|-------------|------------|
| [Zephyr Basics](docs/01_zephyr_basics.md) | Introduction to Zephyr concepts | Beginner |
| [Device Model](docs/02_device_model.md) | Understanding Zephyr's device model | Beginner |
| [Device Tree Guide](docs/03_devicetree_guide.md) | Creating device tree bindings | Intermediate |
| [Kconfig Guide](docs/04_kconfig_guide.md) | Configuration system | Intermediate |
| [Driver Development](docs/05_driver_development.md) | Building custom drivers | Advanced |
| [Testing & Debugging](docs/06_testing_debugging.md) | Testing and debugging drivers | Advanced |

---

## Driver Reference

### Peripheral Drivers

| Driver | Interface | Features |
|--------|-----------|----------|
| [UART Advanced](modules/custom_drivers/drivers/uart_advanced/) | UART | Polling, Interrupt, Async/DMA, Flow Control |
| [SPI Advanced](modules/custom_drivers/drivers/spi_advanced/) | SPI | All modes, DMA, Chip Select, Multi-device |

### Sensor Drivers

| Driver | Interface | Sensor Type |
|--------|-----------|-------------|
| [MPU6050](modules/custom_drivers/drivers/mpu6050/) | I2C | 6-axis IMU (Accel + Gyro) |
| [uBlox NEO-M8N](modules/custom_drivers/drivers/ublox_neo_m8n/) | UART | GPS/GNSS |
| [Ultrasonic](modules/custom_drivers/drivers/ultrasonic/) | GPIO | Distance Sensor (HC-SR04) |

---

## License

Apache-2.0
