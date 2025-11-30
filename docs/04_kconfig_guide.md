# Lesson 4: Kconfig System Guide

## Overview

Kconfig is Zephyr's configuration system, allowing compile-time configuration of features, drivers, and behaviors.

---

## Configuration Hierarchy

```
┌────────────────────────────────────┐
│         Application prj.conf       │  ← Highest priority
├────────────────────────────────────┤
│       Board defconfig files        │
├────────────────────────────────────┤
│         SoC Kconfig files          │
├────────────────────────────────────┤
│      Driver/Subsystem Kconfig      │  ← Default values
└────────────────────────────────────┘
```

---

## Kconfig Syntax

### Basic Options

```kconfig
# Boolean option
config MY_FEATURE
    bool "Enable my feature"
    default y
    help
      Enable this to use my feature.

# Integer option
config MY_BUFFER_SIZE
    int "Buffer size"
    default 256
    range 64 1024
    help
      Size of the internal buffer in bytes.

# String option
config MY_DEVICE_NAME
    string "Device name"
    default "MY_DEV"

# Hex option
config MY_BASE_ADDRESS
    hex "Base address"
    default 0x40000000
```

### Dependencies

```kconfig
config MY_DRIVER
    bool "Enable my driver"
    depends on GPIO
    depends on (I2C || SPI)
    select LOG
    help
      My driver requires GPIO and either I2C or SPI.

# 'depends on' - option only visible if dependency met
# 'select' - automatically enables another option
# 'imply' - suggests but doesn't force another option
```

### Conditional Defaults

```kconfig
config MY_OPTION
    bool "My option"
    default y if BOARD_NRF52840DK
    default n

config MY_VALUE
    int "My value"
    default 100 if SPEED_FAST
    default 50 if SPEED_NORMAL
    default 10
```

### Choice Options

```kconfig
choice MY_MODE
    prompt "Operating mode"
    default MY_MODE_NORMAL

config MY_MODE_NORMAL
    bool "Normal mode"

config MY_MODE_FAST
    bool "Fast mode"

config MY_MODE_LOW_POWER
    bool "Low power mode"

endchoice
```

### Menu Structure

```kconfig
menu "My Driver Configuration"

config MY_DRIVER
    bool "Enable my driver"
    default n

if MY_DRIVER

config MY_DRIVER_BUFFER_SIZE
    int "Buffer size"
    default 256

config MY_DRIVER_LOG_LEVEL
    int "Log level"
    default 3

endif # MY_DRIVER

endmenu
```

---

## Driver Kconfig Template

### Complete Example

```kconfig
# drivers/my_driver/Kconfig

menuconfig MY_DRIVER
    bool "My Driver"
    depends on GPIO
    help
      Enable support for my driver.

if MY_DRIVER

config MY_DRIVER_INIT_PRIORITY
    int "Driver initialization priority"
    default 80
    help
      Device driver initialization priority.

config MY_DRIVER_SHELL
    bool "Enable shell commands"
    depends on SHELL
    default y
    help
      Enable shell commands for my driver.

# Logging configuration (use template)
module = MY_DRIVER
module-str = my_driver
source "subsys/logging/Kconfig.template.log_config"

endif # MY_DRIVER
```

### Using Logging Template

The logging template provides standard log level configuration:

```kconfig
module = MY_DRIVER
module-str = my_driver
source "subsys/logging/Kconfig.template.log_config"
```

This creates `CONFIG_MY_DRIVER_LOG_LEVEL` with values:
- 0: Off
- 1: Error
- 2: Warning
- 3: Info
- 4: Debug

---

## prj.conf Usage

### Basic Configuration

```kconfig
# Enable logging
CONFIG_LOG=y
CONFIG_LOG_MODE_IMMEDIATE=y

# Enable GPIO and I2C
CONFIG_GPIO=y
CONFIG_I2C=y

# Configure my driver
CONFIG_MY_DRIVER=y
CONFIG_MY_DRIVER_BUFFER_SIZE=512
CONFIG_MY_DRIVER_LOG_LEVEL=4

# Serial console
CONFIG_SERIAL=y
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
```

### Board-Specific Configuration

Create `boards/<board>.conf`:

```kconfig
# boards/nrf52840dk_nrf52840.conf
CONFIG_MY_SPECIAL_FEATURE=y
CONFIG_MY_BUFFER_SIZE=1024
```

---

## Accessing Config in C

### Direct Access

```c
#include <zephyr/kernel.h>

/* Boolean - defined or not */
#ifdef CONFIG_MY_FEATURE
    /* Feature enabled */
#endif

#if defined(CONFIG_MY_FEATURE)
    /* Alternative syntax */
#endif

/* Value access */
#if CONFIG_MY_BUFFER_SIZE > 256
    /* Large buffer */
#endif

static uint8_t buffer[CONFIG_MY_BUFFER_SIZE];
```

### IS_ENABLED Macro

```c
/* Preferred for boolean options */
if (IS_ENABLED(CONFIG_MY_FEATURE)) {
    /* This compiles to nothing if disabled */
    do_something();
}

/* With ternary */
int value = IS_ENABLED(CONFIG_FAST_MODE) ? 100 : 50;
```

### COND_CODE Macros

```c
/* Conditional code inclusion */
COND_CODE_1(CONFIG_MY_FEATURE,
    (enabled_code();),
    (disabled_code();))

/* With values */
#define MY_VALUE COND_CODE_1(CONFIG_FAST, (100), (50))
```

---

## Out-of-Tree Kconfig

### Module Kconfig

```kconfig
# modules/custom_drivers/Kconfig

menu "Custom Drivers"

rsource "drivers/uart_advanced/Kconfig"
rsource "drivers/spi_advanced/Kconfig"
rsource "drivers/mpu6050/Kconfig"
rsource "drivers/ublox_neo_m8n/Kconfig"
rsource "drivers/ultrasonic/Kconfig"

endmenu
```

### Driver Kconfig

```kconfig
# drivers/mpu6050/Kconfig

config MPU6050
    bool "MPU6050 IMU Driver"
    depends on I2C
    select SENSOR
    help
      Enable driver for InvenSense MPU6050 6-axis IMU.

if MPU6050

config MPU6050_TRIGGER
    bool "Enable trigger mode"
    depends on GPIO
    help
      Enable interrupt-based data ready trigger.

choice MPU6050_TRIGGER_MODE
    prompt "Trigger mode"
    depends on MPU6050_TRIGGER
    default MPU6050_TRIGGER_GLOBAL_THREAD

config MPU6050_TRIGGER_NONE
    bool "No trigger"

config MPU6050_TRIGGER_GLOBAL_THREAD
    bool "Global thread"
    select MPU6050_TRIGGER

config MPU6050_TRIGGER_OWN_THREAD
    bool "Own thread"
    select MPU6050_TRIGGER

endchoice

config MPU6050_THREAD_PRIORITY
    int "Thread priority"
    depends on MPU6050_TRIGGER_OWN_THREAD
    default 10

config MPU6050_THREAD_STACK_SIZE
    int "Thread stack size"
    depends on MPU6050_TRIGGER_OWN_THREAD
    default 1024

module = MPU6050
module-str = mpu6050
source "subsys/logging/Kconfig.template.log_config"

endif # MPU6050
```

---

## Kconfig Functions

### Utility Functions

```kconfig
# Get environment variable
config MY_PATH
    string
    default "$(ZEPHYR_BASE)/path"

# Conditional on Kconfig symbol
config A
    default y if $(dt_chosen_enabled,zephyr,console)
```

### Device Tree Functions

```kconfig
# Check DT node status
config MY_DT_FEATURE
    default y if $(dt_nodelabel_enabled,my_device)

# Check DT property
config MY_SIZE
    default $(dt_node_int_prop_int,/soc/uart@40000000,buffer-size)
```

---

## Common Configuration Patterns

### Feature Toggles

```kconfig
config MY_ADVANCED_FEATURES
    bool "Enable advanced features"
    default n
    help
      Enable additional features that require more resources.

if MY_ADVANCED_FEATURES

config MY_FEATURE_A
    bool "Feature A"
    default y

config MY_FEATURE_B
    bool "Feature B"
    default y

endif
```

### Hardware Variants

```kconfig
choice MY_HW_VERSION
    prompt "Hardware version"
    default MY_HW_V2

config MY_HW_V1
    bool "Version 1.0"

config MY_HW_V2
    bool "Version 2.0"

config MY_HW_V3
    bool "Version 3.0 (beta)"

endchoice

config MY_HAS_DMA
    bool
    default y if MY_HW_V2 || MY_HW_V3
```

### Size/Performance Tradeoffs

```kconfig
choice MY_OPTIMIZATION
    prompt "Optimization level"
    default MY_OPT_BALANCED

config MY_OPT_SIZE
    bool "Optimize for size"

config MY_OPT_BALANCED
    bool "Balanced"

config MY_OPT_SPEED
    bool "Optimize for speed"

endchoice

config MY_CACHE_SIZE
    int
    default 64 if MY_OPT_SIZE
    default 256 if MY_OPT_BALANCED
    default 1024 if MY_OPT_SPEED
```

---

## Debugging Kconfig

### View Configuration

```bash
# After building, check .config
cat build/zephyr/.config | grep MY_DRIVER

# Interactive configuration
west build -t menuconfig

# GUI configuration
west build -t guiconfig
```

### Common Issues

1. **Option not visible**
   - Check `depends on` requirements
   - Verify parent menu is enabled

2. **Default not applied**
   - Check order of defaults
   - Verify no prj.conf override

3. **Symbol not found**
   - Check Kconfig is included
   - Verify module.yml lists Kconfig

---

## Exercise

Create a Kconfig file for a sensor driver with:

1. Main enable option depending on I2C
2. Choice for polling vs interrupt mode
3. Configurable sample rate (10, 50, 100, 200 Hz)
4. Optional shell commands
5. Logging support

### Solution

```kconfig
menuconfig MY_SENSOR
    bool "My Sensor Driver"
    depends on I2C
    help
      Enable driver for my sensor.

if MY_SENSOR

choice MY_SENSOR_MODE
    prompt "Operating mode"
    default MY_SENSOR_MODE_POLLING

config MY_SENSOR_MODE_POLLING
    bool "Polling mode"

config MY_SENSOR_MODE_INTERRUPT
    bool "Interrupt mode"
    depends on GPIO

endchoice

choice MY_SENSOR_SAMPLE_RATE
    prompt "Sample rate"
    default MY_SENSOR_SAMPLE_RATE_50

config MY_SENSOR_SAMPLE_RATE_10
    bool "10 Hz"

config MY_SENSOR_SAMPLE_RATE_50
    bool "50 Hz"

config MY_SENSOR_SAMPLE_RATE_100
    bool "100 Hz"

config MY_SENSOR_SAMPLE_RATE_200
    bool "200 Hz"

endchoice

config MY_SENSOR_SAMPLE_RATE_VAL
    int
    default 10 if MY_SENSOR_SAMPLE_RATE_10
    default 50 if MY_SENSOR_SAMPLE_RATE_50
    default 100 if MY_SENSOR_SAMPLE_RATE_100
    default 200 if MY_SENSOR_SAMPLE_RATE_200

config MY_SENSOR_SHELL
    bool "Shell commands"
    depends on SHELL
    default y

config MY_SENSOR_INIT_PRIORITY
    int "Init priority"
    default 90

module = MY_SENSOR
module-str = my_sensor
source "subsys/logging/Kconfig.template.log_config"

endif # MY_SENSOR
```

---

## Next Steps

Continue to [Lesson 5: Driver Development](05_driver_development.md) to learn about implementing drivers.
