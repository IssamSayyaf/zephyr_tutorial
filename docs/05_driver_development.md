# Lesson 5: Driver Development Guide

## Overview

This lesson covers the complete process of developing drivers in Zephyr, from architecture design to implementation patterns.

---

## Driver Architecture

### Components of a Driver

```
┌─────────────────────────────────────────────────────────────┐
│                        Driver                               │
├─────────────────────────────────────────────────────────────┤
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │   Header     │  │    Source    │  │   Kconfig    │      │
│  │  (API def)   │  │(Implementation)│ │ (Config)    │      │
│  └──────────────┘  └──────────────┘  └──────────────┘      │
│                                                             │
│  ┌──────────────┐  ┌──────────────┐                        │
│  │  DT Binding  │  │ CMakeLists   │                        │
│  │   (.yaml)    │  │   (.txt)     │                        │
│  └──────────────┘  └──────────────┘                        │
└─────────────────────────────────────────────────────────────┘
```

### File Structure

```
drivers/my_driver/
├── CMakeLists.txt          # Build configuration
├── Kconfig                 # Configuration options
├── include/
│   └── my_driver.h         # Public API header
├── src/
│   └── my_driver.c         # Implementation
└── dts/
    └── bindings/
        └── vendor,my-device.yaml  # Device tree binding
```

---

## Step-by-Step Driver Development

### Step 1: Define the API

```c
/* include/my_driver.h */

#ifndef MY_DRIVER_H_
#define MY_DRIVER_H_

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Driver callback type
 */
typedef void (*my_driver_callback_t)(const struct device *dev,
                                     void *user_data);

/**
 * @brief Driver configuration structure
 */
struct my_driver_config {
    uint32_t param1;
    uint32_t param2;
};

/**
 * @brief Initialize the driver
 *
 * @param dev Device instance
 * @param cfg Configuration parameters
 * @return 0 on success, negative errno on failure
 */
int my_driver_configure(const struct device *dev,
                        const struct my_driver_config *cfg);

/**
 * @brief Read data from device
 *
 * @param dev Device instance
 * @param buf Buffer to store data
 * @param len Maximum bytes to read
 * @return Number of bytes read, or negative errno on failure
 */
int my_driver_read(const struct device *dev, uint8_t *buf, size_t len);

/**
 * @brief Write data to device
 *
 * @param dev Device instance
 * @param buf Data to write
 * @param len Number of bytes to write
 * @return Number of bytes written, or negative errno on failure
 */
int my_driver_write(const struct device *dev, const uint8_t *buf, size_t len);

/**
 * @brief Set callback for async operations
 *
 * @param dev Device instance
 * @param callback Callback function
 * @param user_data User data passed to callback
 * @return 0 on success, negative errno on failure
 */
int my_driver_set_callback(const struct device *dev,
                           my_driver_callback_t callback,
                           void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* MY_DRIVER_H_ */
```

### Step 2: Create the Device Tree Binding

```yaml
# dts/bindings/vendor,my-device.yaml

description: My custom device driver

compatible: "vendor,my-device"

include: base.yaml

properties:
  reg:
    required: true

  interrupts:
    required: false

  param1:
    type: int
    required: true
    description: First configuration parameter

  param2:
    type: int
    required: false
    default: 100
    description: Second configuration parameter

  gpios:
    type: phandle-array
    required: false
    description: Optional GPIO for control
```

### Step 3: Implement the Driver

```c
/* src/my_driver.c */

#define DT_DRV_COMPAT vendor_my_device

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "my_driver.h"

LOG_MODULE_REGISTER(my_driver, CONFIG_MY_DRIVER_LOG_LEVEL);

/* Internal data structure */
struct my_driver_data {
    struct k_sem lock;
    struct k_sem sync;
    my_driver_callback_t callback;
    void *user_data;
    uint8_t rx_buffer[CONFIG_MY_DRIVER_BUFFER_SIZE];
    size_t rx_len;
    bool initialized;
};

/* Configuration structure */
struct my_driver_cfg {
    uint32_t param1;
    uint32_t param2;
    struct gpio_dt_spec gpio;
    void (*irq_config_func)(const struct device *dev);
};

/* API function table */
struct my_driver_api {
    int (*configure)(const struct device *dev,
                     const struct my_driver_config *cfg);
    int (*read)(const struct device *dev, uint8_t *buf, size_t len);
    int (*write)(const struct device *dev, const uint8_t *buf, size_t len);
    int (*set_callback)(const struct device *dev,
                        my_driver_callback_t callback,
                        void *user_data);
};

/* ========== Internal Functions ========== */

static void my_driver_isr(const struct device *dev)
{
    struct my_driver_data *data = dev->data;

    /* Handle interrupt */
    LOG_DBG("Interrupt received");

    /* Signal waiting thread */
    k_sem_give(&data->sync);

    /* Call user callback if set */
    if (data->callback) {
        data->callback(dev, data->user_data);
    }
}

/* ========== API Implementation ========== */

static int my_driver_configure_impl(const struct device *dev,
                                    const struct my_driver_config *cfg)
{
    const struct my_driver_cfg *config = dev->config;
    struct my_driver_data *data = dev->data;
    int ret;

    if (cfg == NULL) {
        return -EINVAL;
    }

    k_sem_take(&data->lock, K_FOREVER);

    /* Apply configuration */
    LOG_INF("Configuring with param1=%u, param2=%u",
            cfg->param1, cfg->param2);

    /* Hardware-specific configuration would go here */

    k_sem_give(&data->lock);
    return 0;
}

static int my_driver_read_impl(const struct device *dev,
                               uint8_t *buf, size_t len)
{
    struct my_driver_data *data = dev->data;
    int ret;

    if (buf == NULL || len == 0) {
        return -EINVAL;
    }

    k_sem_take(&data->lock, K_FOREVER);

    /* Wait for data (with timeout) */
    ret = k_sem_take(&data->sync, K_MSEC(CONFIG_MY_DRIVER_TIMEOUT_MS));
    if (ret < 0) {
        LOG_WRN("Read timeout");
        k_sem_give(&data->lock);
        return -ETIMEDOUT;
    }

    /* Copy data to user buffer */
    size_t copy_len = MIN(len, data->rx_len);
    memcpy(buf, data->rx_buffer, copy_len);

    k_sem_give(&data->lock);
    return copy_len;
}

static int my_driver_write_impl(const struct device *dev,
                                const uint8_t *buf, size_t len)
{
    const struct my_driver_cfg *config = dev->config;
    struct my_driver_data *data = dev->data;

    if (buf == NULL || len == 0) {
        return -EINVAL;
    }

    k_sem_take(&data->lock, K_FOREVER);

    /* Hardware-specific write operation */
    LOG_DBG("Writing %zu bytes", len);

    /* Simulated write - replace with actual hardware access */

    k_sem_give(&data->lock);
    return len;
}

static int my_driver_set_callback_impl(const struct device *dev,
                                       my_driver_callback_t callback,
                                       void *user_data)
{
    struct my_driver_data *data = dev->data;

    k_sem_take(&data->lock, K_FOREVER);

    data->callback = callback;
    data->user_data = user_data;

    k_sem_give(&data->lock);
    return 0;
}

/* API structure */
static const struct my_driver_api my_driver_api_funcs = {
    .configure = my_driver_configure_impl,
    .read = my_driver_read_impl,
    .write = my_driver_write_impl,
    .set_callback = my_driver_set_callback_impl,
};

/* ========== Initialization ========== */

static int my_driver_init(const struct device *dev)
{
    const struct my_driver_cfg *config = dev->config;
    struct my_driver_data *data = dev->data;
    int ret;

    LOG_INF("Initializing %s", dev->name);

    /* Initialize semaphores */
    k_sem_init(&data->lock, 1, 1);
    k_sem_init(&data->sync, 0, 1);

    /* Initialize GPIO if configured */
    if (config->gpio.port != NULL) {
        if (!gpio_is_ready_dt(&config->gpio)) {
            LOG_ERR("GPIO device not ready");
            return -ENODEV;
        }

        ret = gpio_pin_configure_dt(&config->gpio, GPIO_OUTPUT_INACTIVE);
        if (ret < 0) {
            LOG_ERR("Failed to configure GPIO: %d", ret);
            return ret;
        }
    }

    /* Configure interrupt if available */
    if (config->irq_config_func) {
        config->irq_config_func(dev);
    }

    data->initialized = true;
    LOG_INF("Driver initialized (param1=%u, param2=%u)",
            config->param1, config->param2);

    return 0;
}

/* ========== Public API Wrappers ========== */

int my_driver_configure(const struct device *dev,
                        const struct my_driver_config *cfg)
{
    const struct my_driver_api *api = dev->api;

    if (api == NULL || api->configure == NULL) {
        return -ENOTSUP;
    }
    return api->configure(dev, cfg);
}

int my_driver_read(const struct device *dev, uint8_t *buf, size_t len)
{
    const struct my_driver_api *api = dev->api;

    if (api == NULL || api->read == NULL) {
        return -ENOTSUP;
    }
    return api->read(dev, buf, len);
}

int my_driver_write(const struct device *dev, const uint8_t *buf, size_t len)
{
    const struct my_driver_api *api = dev->api;

    if (api == NULL || api->write == NULL) {
        return -ENOTSUP;
    }
    return api->write(dev, buf, len);
}

int my_driver_set_callback(const struct device *dev,
                           my_driver_callback_t callback,
                           void *user_data)
{
    const struct my_driver_api *api = dev->api;

    if (api == NULL || api->set_callback == NULL) {
        return -ENOTSUP;
    }
    return api->set_callback(dev, callback, user_data);
}

/* ========== Device Instantiation ========== */

#define MY_DRIVER_IRQ_CONFIG(inst)                                          \
    static void my_driver_irq_config_##inst(const struct device *dev)       \
    {                                                                        \
        IRQ_CONNECT(DT_INST_IRQN(inst),                                     \
                    DT_INST_IRQ(inst, priority),                            \
                    my_driver_isr,                                           \
                    DEVICE_DT_INST_GET(inst),                               \
                    0);                                                      \
        irq_enable(DT_INST_IRQN(inst));                                     \
    }

#define MY_DRIVER_IRQ_CONFIG_FUNC(inst)                                     \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, interrupts),                   \
                (MY_DRIVER_IRQ_CONFIG(inst)),                               \
                ())

#define MY_DRIVER_IRQ_CONFIG_PTR(inst)                                      \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, interrupts),                   \
                (.irq_config_func = my_driver_irq_config_##inst,),         \
                (.irq_config_func = NULL,))

#define MY_DRIVER_INIT(inst)                                                \
    MY_DRIVER_IRQ_CONFIG_FUNC(inst)                                         \
                                                                            \
    static struct my_driver_data my_driver_data_##inst;                     \
                                                                            \
    static const struct my_driver_cfg my_driver_cfg_##inst = {              \
        .param1 = DT_INST_PROP(inst, param1),                              \
        .param2 = DT_INST_PROP_OR(inst, param2, 100),                      \
        .gpio = GPIO_DT_SPEC_INST_GET_OR(inst, gpios, {0}),                \
        MY_DRIVER_IRQ_CONFIG_PTR(inst)                                      \
    };                                                                      \
                                                                            \
    DEVICE_DT_INST_DEFINE(inst,                                            \
                          my_driver_init,                                   \
                          NULL,                                             \
                          &my_driver_data_##inst,                           \
                          &my_driver_cfg_##inst,                            \
                          POST_KERNEL,                                      \
                          CONFIG_MY_DRIVER_INIT_PRIORITY,                   \
                          &my_driver_api_funcs);

DT_INST_FOREACH_STATUS_OKAY(MY_DRIVER_INIT)
```

### Step 4: Create Kconfig

```kconfig
# Kconfig

menuconfig MY_DRIVER
    bool "My Custom Driver"
    help
      Enable my custom driver.

if MY_DRIVER

config MY_DRIVER_INIT_PRIORITY
    int "Driver initialization priority"
    default 80
    help
      Device driver initialization priority.

config MY_DRIVER_BUFFER_SIZE
    int "Internal buffer size"
    default 256
    range 64 1024
    help
      Size of the internal receive buffer.

config MY_DRIVER_TIMEOUT_MS
    int "Operation timeout (ms)"
    default 1000
    help
      Timeout for blocking operations in milliseconds.

module = MY_DRIVER
module-str = my_driver
source "subsys/logging/Kconfig.template.log_config"

endif # MY_DRIVER
```

### Step 5: Create CMakeLists.txt

```cmake
# CMakeLists.txt

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_MY_DRIVER
    src/my_driver.c
)

zephyr_include_directories(include)
```

---

## Driver Patterns

### Interrupt-Driven I/O

```c
/* ISR handler */
static void driver_isr(const struct device *dev)
{
    struct driver_data *data = dev->data;
    uint8_t status = read_status_register();

    if (status & RX_READY) {
        data->rx_buffer[data->rx_idx++] = read_data_register();
        if (data->rx_idx >= data->rx_expected) {
            k_sem_give(&data->rx_complete);
        }
    }

    if (status & TX_EMPTY) {
        if (data->tx_idx < data->tx_len) {
            write_data_register(data->tx_buffer[data->tx_idx++]);
        } else {
            disable_tx_interrupt();
            k_sem_give(&data->tx_complete);
        }
    }
}
```

### DMA Transfer Pattern

```c
/* DMA callback */
static void dma_callback(const struct device *dma_dev, void *user_data,
                         uint32_t channel, int status)
{
    const struct device *dev = user_data;
    struct driver_data *data = dev->data;

    if (status == 0) {
        data->dma_status = 0;
    } else {
        data->dma_status = -EIO;
    }

    k_sem_give(&data->dma_complete);
}

/* Start DMA transfer */
static int start_dma_transfer(const struct device *dev,
                              void *buf, size_t len)
{
    const struct driver_config *config = dev->config;
    struct driver_data *data = dev->data;

    struct dma_config dma_cfg = {
        .channel_direction = MEMORY_TO_PERIPHERAL,
        .source_data_size = 1,
        .dest_data_size = 1,
        .source_burst_length = 1,
        .dest_burst_length = 1,
        .dma_callback = dma_callback,
        .user_data = (void *)dev,
    };

    struct dma_block_config block_cfg = {
        .source_address = (uint32_t)buf,
        .dest_address = config->base_addr + DATA_REG,
        .block_size = len,
    };

    dma_cfg.head_block = &block_cfg;

    int ret = dma_config(config->dma_dev, config->dma_channel, &dma_cfg);
    if (ret < 0) {
        return ret;
    }

    return dma_start(config->dma_dev, config->dma_channel);
}
```

### Async API Pattern

```c
/* Async read with callback */
int driver_read_async(const struct device *dev,
                      uint8_t *buf, size_t len,
                      driver_callback_t callback,
                      void *user_data)
{
    struct driver_data *data = dev->data;

    k_sem_take(&data->lock, K_FOREVER);

    data->rx_buf = buf;
    data->rx_len = len;
    data->callback = callback;
    data->user_data = user_data;

    /* Enable RX interrupt to start receiving */
    enable_rx_interrupt();

    k_sem_give(&data->lock);
    return 0;
}

/* ISR calls callback when complete */
static void driver_isr(const struct device *dev)
{
    struct driver_data *data = dev->data;

    /* ... receive data ... */

    if (rx_complete) {
        if (data->callback) {
            data->callback(dev, data->rx_len, data->user_data);
        }
    }
}
```

---

## Error Handling

### Standard Error Codes

```c
return -EINVAL;     /* Invalid argument */
return -ENODEV;     /* No such device */
return -EIO;        /* I/O error */
return -EBUSY;      /* Device busy */
return -ETIMEDOUT;  /* Operation timed out */
return -ENOTSUP;    /* Operation not supported */
return -ENOMEM;     /* Out of memory */
return -ENOENT;     /* No such file or directory */
return -EACCES;     /* Permission denied */
```

### Error Checking Pattern

```c
int result = some_operation();
if (result < 0) {
    LOG_ERR("Operation failed: %d", result);
    return result;
}
```

---

## Testing Drivers

### Shell Commands

```c
#ifdef CONFIG_MY_DRIVER_SHELL
#include <zephyr/shell/shell.h>

static int cmd_read(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = device_get_binding("MY_DEVICE");
    uint8_t buf[32];

    int ret = my_driver_read(dev, buf, sizeof(buf));
    if (ret < 0) {
        shell_error(sh, "Read failed: %d", ret);
        return ret;
    }

    shell_hexdump(sh, buf, ret);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(my_driver_cmds,
    SHELL_CMD(read, NULL, "Read from device", cmd_read),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(mydrv, &my_driver_cmds, "My driver commands", NULL);
#endif
```

---

## Next Steps

Continue to [Lesson 6: Testing & Debugging](06_testing_debugging.md) for testing and debugging techniques.
