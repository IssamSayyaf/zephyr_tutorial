# Lesson 3: Device Tree Guide

## Overview

Device Tree is a data structure describing hardware. In Zephyr, it's used to configure hardware without modifying driver code.

---

## Device Tree Structure

### Basic Syntax

```dts
/ {
    /* Root node */

    node_label: node_name@unit_address {
        compatible = "vendor,device";
        reg = <unit_address size>;
        status = "okay";
        property_name = <value>;

        child_node {
            /* Child properties */
        };
    };
};
```

### Property Types

```dts
/* String */
compatible = "vendor,device";
label = "MY_DEVICE";

/* Integer */
reg = <0x40000000 0x1000>;
clock-frequency = <16000000>;

/* Boolean (presence = true) */
read-only;

/* Array */
gpios = <&gpio0 13 GPIO_ACTIVE_LOW>;

/* Phandle (reference to another node) */
clocks = <&clock0>;
```

---

## Bindings

Bindings define the schema for device tree nodes.

### Binding File Structure

```yaml
# vendor,my-device.yaml
description: My custom device driver

compatible: "vendor,my-device"

include: base.yaml

properties:
  reg:
    required: true

  clock-frequency:
    type: int
    required: true
    description: Clock frequency in Hz

  gpios:
    type: phandle-array
    required: true
    description: GPIO specification

  tx-pin:
    type: int
    required: false
    default: 0
    description: TX pin number

  mode:
    type: string
    required: false
    enum:
      - "normal"
      - "fast"
      - "low-power"
    default: "normal"
```

### Property Types in Bindings

| Type | Description | Example |
|------|-------------|---------|
| `int` | Integer | `123` |
| `string` | String | `"hello"` |
| `boolean` | Present/absent | (no value) |
| `array` | Integer array | `<1 2 3>` |
| `uint8-array` | Byte array | `[01 02 03]` |
| `phandle` | Node reference | `<&gpio0>` |
| `phandle-array` | Array of phandles | `<&gpio0 13 0>` |
| `string-array` | String array | `"a", "b"` |

---

## Common Binding Includes

### base.yaml

Basic device properties:
- `status`
- `compatible`
- `reg`
- `interrupts`

### i2c-device.yaml

For I2C devices:
```yaml
include: base.yaml

on-bus: i2c

properties:
  reg:
    required: true
    description: I2C address
```

### spi-device.yaml

For SPI devices:
```yaml
include: base.yaml

on-bus: spi

properties:
  reg:
    required: true
    description: Chip select number

  spi-max-frequency:
    type: int
    required: true
```

---

## Creating Custom Bindings

### Example: Ultrasonic Sensor Binding

```yaml
# vendor,ultrasonic.yaml
description: Ultrasonic distance sensor (HC-SR04)

compatible: "vendor,ultrasonic"

properties:
  trigger-gpios:
    type: phandle-array
    required: true
    description: GPIO for trigger pin

  echo-gpios:
    type: phandle-array
    required: true
    description: GPIO for echo pin

  max-range-cm:
    type: int
    required: false
    default: 400
    description: Maximum detection range in centimeters
```

### Example: I2C Sensor Binding

```yaml
# invensense,mpu6050.yaml
description: MPU6050 6-axis IMU

compatible: "invensense,mpu6050"

include: [sensor-device.yaml, i2c-device.yaml]

properties:
  int-gpios:
    type: phandle-array
    required: false
    description: Interrupt GPIO pin

  gyro-sr-div:
    type: int
    required: false
    default: 0
    description: Gyroscope sample rate divider

  gyro-fs:
    type: int
    required: false
    default: 0
    enum: [0, 1, 2, 3]
    description: |
      Gyroscope full-scale range:
      0 = +/- 250 dps
      1 = +/- 500 dps
      2 = +/- 1000 dps
      3 = +/- 2000 dps

  accel-fs:
    type: int
    required: false
    default: 0
    enum: [0, 1, 2, 3]
    description: |
      Accelerometer full-scale range:
      0 = +/- 2g
      1 = +/- 4g
      2 = +/- 8g
      3 = +/- 16g
```

---

## Device Tree Overlays

Overlays modify the base device tree without editing it directly.

### Creating an Overlay

```dts
/* boards/nrf52840dk_nrf52840.overlay */

/* Enable and configure I2C */
&i2c0 {
    status = "okay";

    mpu6050@68 {
        compatible = "invensense,mpu6050";
        reg = <0x68>;
        int-gpios = <&gpio0 11 GPIO_ACTIVE_HIGH>;
        gyro-fs = <1>;
        accel-fs = <2>;
    };
};

/* Enable UART */
&uart1 {
    status = "okay";
    current-speed = <9600>;

    gps: gps {
        compatible = "u-blox,neo-m8n";
    };
};

/* Add custom node at root */
/ {
    ultrasonic0: ultrasonic {
        compatible = "vendor,ultrasonic";
        trigger-gpios = <&gpio0 3 GPIO_ACTIVE_HIGH>;
        echo-gpios = <&gpio0 4 GPIO_ACTIVE_HIGH>;
    };
};
```

### Overlay Naming Convention

```
boards/
├── nrf52840dk_nrf52840.overlay    # Board-specific
├── stm32f4_disco.overlay          # Another board
└── app.overlay                     # Generic (set via DTC_OVERLAY_FILE)
```

---

## Accessing Device Tree from C

### Node Access Macros

```c
/* By node label */
#define MY_NODE DT_NODELABEL(my_device)

/* By alias */
#define MY_NODE DT_ALIAS(my_alias)

/* By path */
#define MY_NODE DT_PATH(soc, uart_40000000)

/* By chosen */
#define CONSOLE_NODE DT_CHOSEN(zephyr_console)
```

### Property Access Macros

```c
/* Get property values */
uint32_t addr = DT_REG_ADDR(MY_NODE);
uint32_t size = DT_REG_SIZE(MY_NODE);
uint32_t clock = DT_PROP(MY_NODE, clock_frequency);
const char *label = DT_LABEL(MY_NODE);

/* Check if property exists */
#if DT_NODE_HAS_PROP(MY_NODE, optional_prop)
    /* Use property */
#endif

/* GPIO specs */
static const struct gpio_dt_spec gpio =
    GPIO_DT_SPEC_GET(MY_NODE, gpios);
```

### Instance-Based Access

```c
#define DT_DRV_COMPAT vendor_my_device

/* In driver - for each instance */
#define MY_DEV_INIT(inst)                                    \
    uint32_t addr_##inst = DT_INST_REG_ADDR(inst);          \
    uint32_t clock_##inst = DT_INST_PROP(inst, clock_frequency);

/* Iterate over all instances */
DT_INST_FOREACH_STATUS_OKAY(MY_DEV_INIT)
```

---

## Device Tree Macros Reference

### Node Existence

```c
/* Check if node exists and is enabled */
#if DT_NODE_EXISTS(DT_NODELABEL(my_device))
    /* Node exists in DT */
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(my_device), okay)
    /* Node exists and status = "okay" */
#endif
```

### Property Access

```c
/* Basic properties */
DT_PROP(node, property)              /* Get property value */
DT_PROP_OR(node, property, default)  /* With default */
DT_PROP_LEN(node, property)          /* Array length */

/* Register properties */
DT_REG_ADDR(node)                    /* Base address */
DT_REG_SIZE(node)                    /* Size */

/* Interrupt properties */
DT_IRQN(node)                        /* IRQ number */
DT_IRQ(node, irq_cell)               /* IRQ property */

/* Phandle properties */
DT_PHANDLE(node, property)           /* Get phandle */
DT_PHANDLE_BY_IDX(node, prop, idx)   /* Phandle array */
```

### GPIO Macros

```c
/* GPIO spec from DT */
static const struct gpio_dt_spec led =
    GPIO_DT_SPEC_GET(DT_NODELABEL(led0), gpios);

/* Instance version */
static const struct gpio_dt_spec gpio =
    GPIO_DT_SPEC_INST_GET(0, gpios);

/* With flags */
static const struct gpio_dt_spec btn =
    GPIO_DT_SPEC_GET_OR(node, gpios, {0});
```

---

## Best Practices

### 1. Use Descriptive Node Labels

```dts
/* Good */
temp_sensor: tmp102@48 { ... };
accel_main: lis2dh@19 { ... };

/* Avoid */
sensor1: sensor@48 { ... };
dev: device@19 { ... };
```

### 2. Set Proper Status

```dts
/* Disabled by default in SoC DTS */
uart0: uart@40000000 {
    status = "disabled";
};

/* Enable in board or overlay */
&uart0 {
    status = "okay";
};
```

### 3. Use Standard Properties

```dts
/* Use standard property names */
reg = <...>;
interrupts = <...>;
clocks = <...>;
gpios = <...>;

/* Instead of custom names like */
base-address = <...>;  /* Wrong */
irq = <...>;           /* Wrong */
```

### 4. Binding Documentation

```yaml
# Always include description
description: |
  Driver for XYZ sensor.
  Supports modes A, B, C.

properties:
  my-prop:
    description: |
      Detailed explanation of what this property does,
      valid values, and any constraints.
```

---

## Exercise

Create a device tree binding and overlay for a custom RGB LED controller:

1. Three GPIOs for R, G, B
2. Optional PWM frequency property
3. Default brightness level

### Solution

**Binding (vendor,rgb-led.yaml)**:
```yaml
description: RGB LED Controller

compatible: "vendor,rgb-led"

properties:
  red-gpios:
    type: phandle-array
    required: true

  green-gpios:
    type: phandle-array
    required: true

  blue-gpios:
    type: phandle-array
    required: true

  pwm-frequency:
    type: int
    default: 1000

  default-brightness:
    type: int
    default: 50
    description: Default brightness (0-100)
```

**Overlay**:
```dts
/ {
    rgb_led: rgb-led {
        compatible = "vendor,rgb-led";
        red-gpios = <&gpio0 13 GPIO_ACTIVE_HIGH>;
        green-gpios = <&gpio0 14 GPIO_ACTIVE_HIGH>;
        blue-gpios = <&gpio0 15 GPIO_ACTIVE_HIGH>;
        pwm-frequency = <2000>;
        default-brightness = <75>;
    };
};
```

---

## Next Steps

Continue to [Lesson 4: Kconfig Guide](04_kconfig_guide.md) to learn about the configuration system.
