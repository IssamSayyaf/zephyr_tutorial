# Lesson 2: Zephyr Device Model

## Overview

Zephyr's device model provides a unified interface for hardware abstraction. Understanding this model is essential for developing drivers.

---

## Device Model Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      Application                            │
├─────────────────────────────────────────────────────────────┤
│                      Driver API                             │
│              (gpio.h, i2c.h, spi.h, uart.h, etc.)          │
├─────────────────────────────────────────────────────────────┤
│                    Device Driver                            │
│            (hardware-specific implementation)               │
├─────────────────────────────────────────────────────────────┤
│                      Hardware                               │
└─────────────────────────────────────────────────────────────┘
```

---

## Core Components

### 1. Device Structure

Every device in Zephyr is represented by `struct device`:

```c
struct device {
    const char *name;           /* Device name */
    const void *config;         /* Configuration (ROM) */
    const void *api;            /* Driver API */
    void *data;                 /* Runtime data (RAM) */
    /* ... other fields ... */
};
```

### 2. Configuration Structure (Read-Only)

Stores device configuration in ROM:

```c
struct my_driver_config {
    uint32_t base_address;
    uint32_t clock_frequency;
    uint8_t irq_number;
    const struct gpio_dt_spec gpio;
};
```

### 3. Data Structure (Read-Write)

Stores runtime state in RAM:

```c
struct my_driver_data {
    bool initialized;
    uint32_t counter;
    struct k_sem lock;
    my_callback_t callback;
    void *user_data;
};
```

### 4. API Structure

Function pointers for driver operations:

```c
typedef int (*my_driver_init_t)(const struct device *dev);
typedef int (*my_driver_read_t)(const struct device *dev, uint8_t *buf, size_t len);
typedef int (*my_driver_write_t)(const struct device *dev, const uint8_t *buf, size_t len);

struct my_driver_api {
    my_driver_init_t init;
    my_driver_read_t read;
    my_driver_write_t write;
};
```

---

## Device Definition Macros

### DEVICE_DEFINE

Basic device definition:

```c
DEVICE_DEFINE(my_device,           /* Device name symbol */
              "MY_DEVICE",          /* Device name string */
              my_device_init,       /* Init function */
              NULL,                 /* PM control (optional) */
              &my_device_data,      /* Runtime data */
              &my_device_config,    /* Configuration */
              POST_KERNEL,          /* Initialization level */
              CONFIG_MY_INIT_PRIO,  /* Priority */
              &my_driver_api);      /* API structure */
```

### DEVICE_DT_DEFINE

Device definition from Device Tree:

```c
DEVICE_DT_DEFINE(DT_NODELABEL(my_device),
                 my_device_init,
                 NULL,
                 &my_device_data,
                 &my_device_config,
                 POST_KERNEL,
                 CONFIG_MY_INIT_PRIO,
                 &my_driver_api);
```

### DEVICE_DT_INST_DEFINE

Instance-based definition (most common):

```c
#define MY_DRIVER_INIT(inst)                                    \
    static struct my_driver_data my_data_##inst;                \
                                                                \
    static const struct my_driver_config my_config_##inst = {   \
        .base_address = DT_INST_REG_ADDR(inst),                \
        .irq = DT_INST_IRQN(inst),                             \
    };                                                          \
                                                                \
    DEVICE_DT_INST_DEFINE(inst,                                \
                          my_driver_init,                       \
                          NULL,                                 \
                          &my_data_##inst,                      \
                          &my_config_##inst,                    \
                          POST_KERNEL,                          \
                          CONFIG_MY_DRIVER_INIT_PRIORITY,       \
                          &my_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MY_DRIVER_INIT)
```

---

## Initialization Levels

Devices are initialized in a specific order:

| Level | Purpose | Example Drivers |
|-------|---------|-----------------|
| `EARLY` | Very early init | Clock, memory |
| `PRE_KERNEL_1` | Before kernel | Basic peripherals |
| `PRE_KERNEL_2` | Before kernel | More peripherals |
| `POST_KERNEL` | After kernel | Most drivers |
| `APPLICATION` | Application-level | High-level drivers |

### Initialization Priority

Within each level, priority determines order (0-99):
- Lower number = earlier initialization
- Use `CONFIG_*_INIT_PRIORITY` in Kconfig

```c
/* In prj.conf */
CONFIG_GPIO_INIT_PRIORITY=40
CONFIG_I2C_INIT_PRIORITY=50
CONFIG_SENSOR_INIT_PRIORITY=90  /* After I2C */
```

---

## Device Tree Integration

### Accessing Device Tree Properties

```c
/* Get node label */
#define MY_NODE DT_NODELABEL(my_device)

/* Get properties */
#define BASE_ADDR DT_REG_ADDR(MY_NODE)
#define IRQ_NUM   DT_IRQN(MY_NODE)
#define LABEL     DT_LABEL(MY_NODE)

/* Instance-based access */
#define DT_DRV_COMPAT vendor_my_device

#define BASE_ADDR_INST(inst) DT_INST_REG_ADDR(inst)
#define IRQ_INST(inst)       DT_INST_IRQN(inst)
```

### GPIO from Device Tree

```c
/* Device tree */
my_device: my_device@0 {
    compatible = "vendor,my-device";
    gpios = <&gpio0 13 GPIO_ACTIVE_LOW>;
};

/* Driver code */
static const struct my_driver_config config = {
    .gpio = GPIO_DT_SPEC_INST_GET(0, gpios),
};

/* Using the GPIO */
gpio_pin_configure_dt(&config->gpio, GPIO_OUTPUT);
gpio_pin_set_dt(&config->gpio, 1);
```

---

## Getting Device References

### By Name

```c
const struct device *dev = device_get_binding("UART_0");
if (!device_is_ready(dev)) {
    LOG_ERR("Device not ready");
    return -ENODEV;
}
```

### From Device Tree (Preferred)

```c
/* Using node label */
const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(uart0));

/* Using alias */
const struct device *dev = DEVICE_DT_GET(DT_ALIAS(my_uart));

/* Using chosen */
const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

/* Always check readiness */
if (!device_is_ready(dev)) {
    LOG_ERR("Device not ready");
    return -ENODEV;
}
```

---

## Complete Driver Example

### Header File (my_driver.h)

```c
#ifndef MY_DRIVER_H
#define MY_DRIVER_H

#include <zephyr/device.h>

/* API functions */
int my_driver_read(const struct device *dev, uint8_t *buf, size_t len);
int my_driver_write(const struct device *dev, const uint8_t *buf, size_t len);
int my_driver_configure(const struct device *dev, uint32_t config);

#endif /* MY_DRIVER_H */
```

### Source File (my_driver.c)

```c
#define DT_DRV_COMPAT vendor_my_device

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include "my_driver.h"

LOG_MODULE_REGISTER(my_driver, CONFIG_MY_DRIVER_LOG_LEVEL);

struct my_driver_data {
    struct k_sem lock;
    bool configured;
};

struct my_driver_config {
    uint32_t base_address;
    uint32_t clock_freq;
};

/* Driver API implementation */
static int my_driver_read_impl(const struct device *dev, uint8_t *buf, size_t len)
{
    const struct my_driver_config *config = dev->config;
    struct my_driver_data *data = dev->data;

    k_sem_take(&data->lock, K_FOREVER);

    /* Read implementation */
    LOG_DBG("Reading %zu bytes from 0x%08x", len, config->base_address);

    k_sem_give(&data->lock);
    return 0;
}

static int my_driver_write_impl(const struct device *dev,
                                const uint8_t *buf, size_t len)
{
    const struct my_driver_config *config = dev->config;
    struct my_driver_data *data = dev->data;

    k_sem_take(&data->lock, K_FOREVER);

    /* Write implementation */
    LOG_DBG("Writing %zu bytes to 0x%08x", len, config->base_address);

    k_sem_give(&data->lock);
    return 0;
}

/* API structure */
static const struct my_driver_api api = {
    .read = my_driver_read_impl,
    .write = my_driver_write_impl,
};

/* Initialization */
static int my_driver_init(const struct device *dev)
{
    const struct my_driver_config *config = dev->config;
    struct my_driver_data *data = dev->data;

    k_sem_init(&data->lock, 1, 1);

    LOG_INF("Initialized at 0x%08x, clock=%u Hz",
            config->base_address, config->clock_freq);

    return 0;
}

/* Device instantiation macro */
#define MY_DRIVER_INIT(inst)                                           \
    static struct my_driver_data my_driver_data_##inst;                \
                                                                       \
    static const struct my_driver_config my_driver_config_##inst = {   \
        .base_address = DT_INST_REG_ADDR(inst),                       \
        .clock_freq = DT_INST_PROP(inst, clock_frequency),            \
    };                                                                 \
                                                                       \
    DEVICE_DT_INST_DEFINE(inst,                                       \
                          my_driver_init,                              \
                          NULL,                                        \
                          &my_driver_data_##inst,                      \
                          &my_driver_config_##inst,                    \
                          POST_KERNEL,                                 \
                          CONFIG_MY_DRIVER_INIT_PRIORITY,              \
                          &api);

/* Instantiate for all enabled nodes */
DT_INST_FOREACH_STATUS_OKAY(MY_DRIVER_INIT)

/* Public API wrappers */
int my_driver_read(const struct device *dev, uint8_t *buf, size_t len)
{
    const struct my_driver_api *api = dev->api;
    return api->read(dev, buf, len);
}

int my_driver_write(const struct device *dev, const uint8_t *buf, size_t len)
{
    const struct my_driver_api *api = dev->api;
    return api->write(dev, buf, len);
}
```

---

## Device Model Best Practices

### 1. Always Check Device Readiness

```c
if (!device_is_ready(dev)) {
    LOG_ERR("Device %s not ready", dev->name);
    return -ENODEV;
}
```

### 2. Use Proper Error Codes

```c
/* Return standard error codes */
return -EINVAL;   /* Invalid argument */
return -ENODEV;   /* No such device */
return -EIO;      /* I/O error */
return -EBUSY;    /* Device busy */
return -ETIMEDOUT; /* Operation timed out */
```

### 3. Thread Safety

```c
struct my_driver_data {
    struct k_sem lock;
};

static int my_operation(const struct device *dev)
{
    struct my_driver_data *data = dev->data;
    int ret;

    k_sem_take(&data->lock, K_FOREVER);
    ret = do_hardware_operation();
    k_sem_give(&data->lock);

    return ret;
}
```

### 4. Configuration vs Data

- **Config**: Hardware settings, GPIO specs, addresses (ROM)
- **Data**: State, counters, callbacks, buffers (RAM)

---

## Exercise

Create a simple LED driver with the device model:

1. Define config structure with GPIO spec
2. Define data structure with state
3. Implement init, on, off, toggle functions
4. Use DEVICE_DT_INST_DEFINE

---

## Next Steps

Continue to [Lesson 3: Device Tree Guide](03_devicetree_guide.md) to learn about Device Tree bindings and overlays.
