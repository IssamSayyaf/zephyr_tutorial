# Zephyr Driver Skeleton Template - Complete Guide

This document provides a complete skeleton template for building Zephyr drivers with detailed explanations of every component, why it exists, and how to use it.

## Table of Contents

1. [Driver Architecture Overview](#driver-architecture-overview)
2. [The Complete Skeleton](#the-complete-skeleton)
3. [Component-by-Component Breakdown](#component-by-component-breakdown)
4. [File Structure Template](#file-structure-template)
5. [Step-by-Step Implementation Guide](#step-by-step-implementation-guide)

---

## Driver Architecture Overview

### Why Zephyr Uses This Architecture

Zephyr's driver model is designed around several key principles:

```
┌─────────────────────────────────────────────────────────────────┐
│                     APPLICATION LAYER                           │
│                  (Your application code)                        │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼ Uses generic API
┌─────────────────────────────────────────────────────────────────┐
│                      DRIVER API LAYER                           │
│            (struct my_driver_api - function pointers)           │
│                                                                 │
│  WHY: Provides abstraction so applications don't depend on      │
│       specific hardware implementations                         │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼ Implements API
┌─────────────────────────────────────────────────────────────────┐
│                   DRIVER IMPLEMENTATION                         │
│              (The actual hardware-specific code)                │
│                                                                 │
│  WHY: Contains all hardware-specific details, isolated from     │
│       application code                                          │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼ Configured by
┌─────────────────────────────────────────────────────────────────┐
│                    DEVICE TREE + KCONFIG                        │
│         (Hardware description + Build-time options)             │
│                                                                 │
│  WHY: Separates hardware description from code, enables         │
│       same driver to work on different boards                   │
└─────────────────────────────────────────────────────────────────┘
```

### The Three Core Data Structures

Every Zephyr driver has THREE essential structures:

```c
/**
 * 1. CONFIG STRUCTURE (const, in ROM/Flash)
 *
 * PURPOSE: Store hardware configuration that NEVER changes at runtime
 * LOCATION: Stored in Flash/ROM to save RAM
 * EXAMPLES: Pin numbers, I2C addresses, IRQ numbers, base addresses
 *
 * WHY CONST: These values come from Device Tree and are fixed for
 *            a specific board. Storing in ROM saves precious RAM.
 */
struct my_driver_config {
    /* Hardware resources - from Device Tree */
    const struct device *bus;      /* Parent bus (I2C, SPI, etc.) */
    uint32_t base_address;         /* Memory-mapped peripheral address */
    uint8_t slave_address;         /* I2C slave address */
    uint8_t irq_number;            /* Interrupt number */

    /* GPIO specifications */
    struct gpio_dt_spec interrupt_gpio;
    struct gpio_dt_spec reset_gpio;

    /* Configuration options - from Kconfig via Device Tree */
    uint32_t clock_frequency;
    uint8_t default_mode;
};

/**
 * 2. DATA STRUCTURE (mutable, in RAM)
 *
 * PURPOSE: Store runtime state that CHANGES during operation
 * LOCATION: Stored in RAM
 * EXAMPLES: Current settings, buffers, statistics, synchronization objects
 *
 * WHY MUTABLE: These values change as the driver operates -
 *              counters increment, buffers fill, states change.
 */
struct my_driver_data {
    /* Synchronization primitives */
    struct k_sem lock;             /* Protect concurrent access */
    struct k_sem data_ready;       /* Signal data availability */

    /* Runtime state */
    bool initialized;
    bool enabled;
    uint8_t current_mode;

    /* Data buffers */
    uint8_t rx_buffer[256];
    uint8_t tx_buffer[256];
    size_t rx_count;

    /* Statistics */
    uint32_t transfer_count;
    uint32_t error_count;

    /* Callback storage */
    my_driver_callback_t callback;
    void *callback_user_data;
};

/**
 * 3. API STRUCTURE (const, function pointers)
 *
 * PURPOSE: Define the interface that applications use
 * LOCATION: Stored in ROM (function pointers don't change)
 *
 * WHY FUNCTION POINTERS: Enables polymorphism - different hardware
 *                        implementations can provide same interface
 */
struct my_driver_api {
    int (*init)(const struct device *dev);
    int (*read)(const struct device *dev, uint8_t *buf, size_t len);
    int (*write)(const struct device *dev, const uint8_t *buf, size_t len);
    int (*configure)(const struct device *dev, uint32_t config);
};
```

---

## The Complete Skeleton

### Header File Template (`include/my_driver.h`)

```c
/**
 * @file my_driver.h
 * @brief Public API for My Driver
 *
 * DESIGN PRINCIPLE: The header file is the CONTRACT between your driver
 * and applications. It should:
 * - Define all public data types
 * - Declare all public functions
 * - Hide all implementation details
 * - Be self-documenting with clear comments
 */

#ifndef MY_DRIVER_H_
#define MY_DRIVER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * =============================================================================
 * SECTION 1: CONSTANTS AND CONFIGURATION VALUES
 * =============================================================================
 *
 * WHY: Define magic numbers as named constants for:
 * - Self-documenting code
 * - Single point of change
 * - Type safety (enums vs raw integers)
 */

/** Driver version for compatibility checking */
#define MY_DRIVER_VERSION_MAJOR 1
#define MY_DRIVER_VERSION_MINOR 0

/** Maximum buffer sizes */
#define MY_DRIVER_MAX_TRANSFER_SIZE  4096
#define MY_DRIVER_DEFAULT_TIMEOUT_MS 1000

/**
 * Operating modes
 *
 * WHY ENUM: Compiler can check for valid values, IDE can autocomplete,
 *           debugger shows meaningful names instead of numbers
 */
enum my_driver_mode {
    MY_DRIVER_MODE_POLLING = 0,    /**< CPU polls for completion */
    MY_DRIVER_MODE_INTERRUPT = 1,   /**< ISR signals completion */
    MY_DRIVER_MODE_DMA = 2,         /**< DMA handles transfer */
};

/**
 * Error codes specific to this driver
 *
 * WHY NEGATIVE: Zephyr convention - negative = error, 0 = success, positive = info
 * WHY CUSTOM CODES: Generic -EIO doesn't tell you WHAT failed
 */
enum my_driver_error {
    MY_DRIVER_OK = 0,
    MY_DRIVER_ERR_NOT_READY = -100,     /**< Device not initialized */
    MY_DRIVER_ERR_BUSY = -101,          /**< Transfer in progress */
    MY_DRIVER_ERR_TIMEOUT = -102,       /**< Operation timed out */
    MY_DRIVER_ERR_INVALID_PARAM = -103, /**< Invalid parameter */
    MY_DRIVER_ERR_HARDWARE = -104,      /**< Hardware error */
};

/**
 * =============================================================================
 * SECTION 2: DATA STRUCTURES FOR USERS
 * =============================================================================
 *
 * WHY: Applications need structures to pass data to/from driver
 */

/**
 * Configuration structure for driver setup
 *
 * Users fill this to configure the driver behavior
 */
struct my_driver_user_config {
    enum my_driver_mode mode;      /**< Operating mode */
    uint32_t speed_hz;             /**< Communication speed */
    uint32_t timeout_ms;           /**< Operation timeout */
    bool auto_retry;               /**< Retry on errors */
    uint8_t max_retries;           /**< Maximum retry count */
};

/**
 * Data structure returned by read operations
 */
struct my_driver_reading {
    int32_t value;                 /**< Primary reading value */
    int32_t secondary_value;       /**< Secondary reading (if applicable) */
    uint32_t timestamp;            /**< Kernel timestamp of reading */
    uint8_t status;                /**< Status flags */
    bool valid;                    /**< True if reading is valid */
};

/**
 * Statistics structure for monitoring
 */
struct my_driver_stats {
    uint32_t total_transfers;      /**< Total transfer count */
    uint32_t successful_transfers; /**< Successful transfers */
    uint32_t failed_transfers;     /**< Failed transfers */
    uint32_t retries;              /**< Retry count */
    uint32_t last_error;           /**< Last error code */
    uint64_t bytes_transferred;    /**< Total bytes transferred */
};

/**
 * =============================================================================
 * SECTION 3: CALLBACK DEFINITIONS
 * =============================================================================
 *
 * WHY CALLBACKS: Enable asynchronous notification without polling
 * Applications register a callback, driver calls it when events occur
 */

/**
 * Event types that trigger callbacks
 */
enum my_driver_event {
    MY_DRIVER_EVENT_DATA_READY,    /**< New data available */
    MY_DRIVER_EVENT_ERROR,         /**< Error occurred */
    MY_DRIVER_EVENT_TRANSFER_DONE, /**< Transfer completed */
    MY_DRIVER_EVENT_THRESHOLD,     /**< Threshold crossed */
};

/**
 * Callback function type
 *
 * @param dev       Device that generated the event
 * @param event     Type of event
 * @param user_data User-provided context pointer
 *
 * WHY THIS SIGNATURE:
 * - dev: Allows one callback to handle multiple devices
 * - event: Tells callback what happened
 * - user_data: Allows passing context without global variables
 *
 * IMPORTANT: Callbacks may run in ISR context! Keep them SHORT.
 */
typedef void (*my_driver_callback_t)(const struct device *dev,
                                      enum my_driver_event event,
                                      void *user_data);

/**
 * =============================================================================
 * SECTION 4: PUBLIC API FUNCTIONS
 * =============================================================================
 *
 * DESIGN PRINCIPLES:
 * 1. First parameter is always `const struct device *dev`
 * 2. Return int (0 = success, negative = error)
 * 3. Use const for input parameters, pointers for output
 * 4. Document all parameters and return values
 */

/**
 * @brief Configure the driver
 *
 * @param dev    Device instance
 * @param config Configuration to apply
 *
 * @retval 0        Success
 * @retval -EINVAL  Invalid configuration
 * @retval -EBUSY   Device busy, cannot reconfigure
 * @retval -EIO     Hardware error
 *
 * WHEN TO CALL: After device is ready, before first use
 * THREAD SAFETY: Thread-safe, uses internal locking
 */
int my_driver_configure(const struct device *dev,
                        const struct my_driver_user_config *config);

/**
 * @brief Read data from the device
 *
 * @param dev     Device instance
 * @param reading Pointer to store the reading
 *
 * @retval 0         Success
 * @retval -ENODATA  No data available
 * @retval -ETIMEDOUT Read timed out
 * @retval -EIO      Hardware error
 *
 * BLOCKING: This function blocks until data is available or timeout
 * THREAD SAFETY: Thread-safe
 */
int my_driver_read(const struct device *dev, struct my_driver_reading *reading);

/**
 * @brief Read data with custom timeout
 *
 * @param dev        Device instance
 * @param reading    Pointer to store the reading
 * @param timeout    Timeout in milliseconds (K_FOREVER for infinite)
 *
 * @retval 0         Success
 * @retval -EAGAIN   Timeout (K_NO_WAIT) and no data available
 * @retval -ETIMEDOUT Timeout expired
 */
int my_driver_read_timeout(const struct device *dev,
                           struct my_driver_reading *reading,
                           k_timeout_t timeout);

/**
 * @brief Write data to the device
 *
 * @param dev  Device instance
 * @param buf  Buffer containing data to write
 * @param len  Number of bytes to write
 *
 * @retval >= 0  Number of bytes written
 * @retval -EINVAL Invalid parameters
 * @retval -EIO    Hardware error
 */
int my_driver_write(const struct device *dev, const uint8_t *buf, size_t len);

/**
 * @brief Start asynchronous operation
 *
 * @param dev      Device instance
 * @param callback Callback for completion notification
 * @param user_data User data passed to callback
 *
 * @retval 0      Operation started
 * @retval -EBUSY Previous operation still in progress
 *
 * NON-BLOCKING: Returns immediately, callback called on completion
 */
int my_driver_async_read(const struct device *dev,
                         my_driver_callback_t callback,
                         void *user_data);

/**
 * @brief Register event callback
 *
 * @param dev       Device instance
 * @param callback  Callback function (NULL to unregister)
 * @param user_data User data for callback
 *
 * @retval 0 Success
 */
int my_driver_set_callback(const struct device *dev,
                           my_driver_callback_t callback,
                           void *user_data);

/**
 * @brief Get driver statistics
 *
 * @param dev   Device instance
 * @param stats Pointer to store statistics
 *
 * @retval 0 Success
 */
int my_driver_get_stats(const struct device *dev, struct my_driver_stats *stats);

/**
 * @brief Reset driver statistics
 *
 * @param dev Device instance
 *
 * @retval 0 Success
 */
int my_driver_reset_stats(const struct device *dev);

/**
 * @brief Enable the device
 *
 * @param dev Device instance
 *
 * @retval 0     Success
 * @retval -EIO  Hardware error
 */
int my_driver_enable(const struct device *dev);

/**
 * @brief Disable the device
 *
 * @param dev Device instance
 *
 * @retval 0 Success
 */
int my_driver_disable(const struct device *dev);

/**
 * =============================================================================
 * SECTION 5: CONVENIENCE MACROS
 * =============================================================================
 *
 * WHY: Make common operations easier for users
 */

/**
 * Get device instance by Device Tree node label
 *
 * Usage: const struct device *dev = MY_DRIVER_DEVICE_GET(my_sensor);
 *
 * WHY MACRO: Combines device lookup with compile-time node validation
 */
#define MY_DRIVER_DEVICE_GET(node_label) \
    DEVICE_DT_GET(DT_NODELABEL(node_label))

/**
 * Check if driver is ready for use
 *
 * WHY: Convenience wrapper around device_is_ready()
 */
static inline bool my_driver_is_ready(const struct device *dev)
{
    return device_is_ready(dev);
}

#ifdef __cplusplus
}
#endif

#endif /* MY_DRIVER_H_ */
```

### Source File Template (`src/my_driver.c`)

```c
/**
 * @file my_driver.c
 * @brief Implementation of My Driver
 *
 * DESIGN PRINCIPLES FOR IMPLEMENTATION:
 * 1. Include logging at strategic points
 * 2. Validate all inputs
 * 3. Handle all error cases
 * 4. Use proper synchronization
 * 5. Keep ISRs minimal
 */

#define DT_DRV_COMPAT my_vendor_my_device

/**
 * WHY THIS DEFINE:
 * DT_DRV_COMPAT must be defined BEFORE including any Zephyr headers.
 * It tells the DT_INST_* macros which compatible string to use.
 * The format is: vendor_device with underscores replacing commas/hyphens.
 *
 * Device Tree binding: compatible = "my-vendor,my-device"
 * DT_DRV_COMPAT value: my_vendor_my_device
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>  /* or spi.h, uart.h, etc. */
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

/* Include our public header */
#include "my_driver.h"

/**
 * Register logging module
 *
 * WHY: Enables runtime log level control per-module
 * The level (LOG_LEVEL_INF) is the DEFAULT - can be overridden in prj.conf
 */
LOG_MODULE_REGISTER(my_driver, CONFIG_MY_DRIVER_LOG_LEVEL);

/**
 * =============================================================================
 * SECTION 1: PRIVATE DATA STRUCTURES
 * =============================================================================
 *
 * These are INTERNAL to the driver - not exposed in header
 */

/**
 * Hardware configuration (from Device Tree)
 *
 * WHY CONST: These values never change at runtime
 * WHY THIS SPECIFIC STRUCT: Matches what we extract from Device Tree
 */
struct my_driver_config {
    /* Parent bus device */
    const struct device *bus;

    /* Device address on bus */
    uint16_t addr;

    /* GPIO for interrupt (optional) */
    struct gpio_dt_spec int_gpio;

    /* Configuration from DT properties */
    uint32_t sample_rate;
    uint8_t resolution;
    bool interrupt_enabled;
};

/**
 * Runtime data
 *
 * WHY THESE FIELDS:
 * - lock: Prevent concurrent access from multiple threads
 * - data_ready_sem: Efficient waiting for data (vs polling)
 * - callback/user_data: Async notification support
 * - current_*: Track runtime state
 */
struct my_driver_data {
    /* Synchronization */
    struct k_sem lock;              /* Mutex for thread safety */
    struct k_sem data_ready_sem;    /* Signaled when data available */

    /* Callback for async events */
    my_driver_callback_t callback;
    void *callback_user_data;

    /* Runtime configuration */
    struct my_driver_user_config user_config;

    /* Current state */
    bool enabled;
    bool transfer_in_progress;

    /* Latest reading */
    struct my_driver_reading last_reading;

    /* Statistics */
    struct my_driver_stats stats;

    /* Work queue for deferred processing */
    struct k_work process_work;
    const struct device *dev;  /* Back-reference for work handler */

    /* GPIO callback (for interrupt) */
    struct gpio_callback int_gpio_cb;
};

/**
 * =============================================================================
 * SECTION 2: INTERNAL HELPER FUNCTIONS
 * =============================================================================
 *
 * WHY STATIC: These are private to this file
 * WHY SEPARATE: Keep public API functions clean and readable
 */

/**
 * Read register(s) from device via I2C
 *
 * WHY THIS ABSTRACTION: Centralizes bus communication, makes it easy to:
 * - Add error handling in one place
 * - Change bus protocol without touching higher-level code
 * - Add retry logic if needed
 */
static int reg_read(const struct device *dev, uint8_t reg, uint8_t *buf, size_t len)
{
    const struct my_driver_config *config = dev->config;
    int ret;

    /* Write register address, then read data */
    ret = i2c_write_read(config->bus, config->addr, &reg, 1, buf, len);
    if (ret < 0) {
        LOG_ERR("Failed to read reg 0x%02X: %d", reg, ret);
    }

    return ret;
}

/**
 * Write register(s) to device via I2C
 */
static int reg_write(const struct device *dev, uint8_t reg, const uint8_t *buf, size_t len)
{
    const struct my_driver_config *config = dev->config;
    uint8_t tx_buf[16];  /* Register + data */
    int ret;

    /* Validate length */
    if (len > sizeof(tx_buf) - 1) {
        LOG_ERR("Write too large: %zu > %zu", len, sizeof(tx_buf) - 1);
        return -EINVAL;
    }

    /* Build message: [register][data...] */
    tx_buf[0] = reg;
    memcpy(&tx_buf[1], buf, len);

    ret = i2c_write(config->bus, tx_buf, len + 1, config->addr);
    if (ret < 0) {
        LOG_ERR("Failed to write reg 0x%02X: %d", reg, ret);
    }

    return ret;
}

/**
 * Single register write helper
 */
static inline int reg_write_byte(const struct device *dev, uint8_t reg, uint8_t value)
{
    return reg_write(dev, reg, &value, 1);
}

/**
 * GPIO interrupt handler
 *
 * WHY MINIMAL: ISRs should be FAST
 * - Don't do I2C/SPI communication here (too slow)
 * - Don't call blocking functions
 * - Just signal and return
 */
static void int_gpio_handler(const struct device *gpio_dev,
                             struct gpio_callback *cb,
                             uint32_t pins)
{
    /* Get data structure from callback */
    struct my_driver_data *data = CONTAINER_OF(cb, struct my_driver_data, int_gpio_cb);

    /* Schedule deferred work (runs in system workqueue thread) */
    k_work_submit(&data->process_work);
}

/**
 * Deferred interrupt processing
 *
 * WHY WORK QUEUE:
 * - Runs in thread context (not ISR)
 * - Can do I2C/SPI communication
 * - Can call blocking functions if needed
 * - Doesn't block the ISR
 */
static void process_work_handler(struct k_work *work)
{
    struct my_driver_data *data = CONTAINER_OF(work, struct my_driver_data, process_work);
    const struct device *dev = data->dev;
    int ret;

    LOG_DBG("Processing interrupt");

    /* Read data from device */
    ret = reg_read(dev, 0x00 /* DATA_REG */,
                   (uint8_t *)&data->last_reading,
                   sizeof(data->last_reading));

    if (ret == 0) {
        data->last_reading.valid = true;
        data->last_reading.timestamp = k_uptime_get_32();
        data->stats.successful_transfers++;

        /* Signal any waiting threads */
        k_sem_give(&data->data_ready_sem);

        /* Call user callback if registered */
        if (data->callback != NULL) {
            data->callback(dev, MY_DRIVER_EVENT_DATA_READY, data->callback_user_data);
        }
    } else {
        data->stats.failed_transfers++;
        data->stats.last_error = ret;

        /* Notify error via callback */
        if (data->callback != NULL) {
            data->callback(dev, MY_DRIVER_EVENT_ERROR, data->callback_user_data);
        }
    }

    data->stats.total_transfers++;
}

/**
 * =============================================================================
 * SECTION 3: API FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

/**
 * Configure the driver
 */
static int my_driver_configure_impl(const struct device *dev,
                                    const struct my_driver_user_config *config)
{
    struct my_driver_data *data = dev->data;
    int ret = 0;

    /* Validate parameters */
    if (config == NULL) {
        LOG_ERR("NULL config");
        return -EINVAL;
    }

    if (config->speed_hz == 0 || config->speed_hz > 1000000) {
        LOG_ERR("Invalid speed: %u", config->speed_hz);
        return -EINVAL;
    }

    /* Acquire lock - prevents concurrent access */
    ret = k_sem_take(&data->lock, K_MSEC(1000));
    if (ret != 0) {
        LOG_ERR("Failed to acquire lock");
        return -EBUSY;
    }

    /* Store configuration */
    memcpy(&data->user_config, config, sizeof(*config));

    /* Apply configuration to hardware */
    /* ... hardware-specific configuration code ... */

    LOG_INF("Configured: mode=%d, speed=%u Hz", config->mode, config->speed_hz);

    /* Release lock */
    k_sem_give(&data->lock);

    return ret;
}

/**
 * Read data from device
 */
static int my_driver_read_impl(const struct device *dev,
                               struct my_driver_reading *reading)
{
    return my_driver_read_timeout(dev, reading, K_MSEC(MY_DRIVER_DEFAULT_TIMEOUT_MS));
}

/**
 * Read with timeout
 */
int my_driver_read_timeout(const struct device *dev,
                           struct my_driver_reading *reading,
                           k_timeout_t timeout)
{
    struct my_driver_data *data = dev->data;
    int ret;

    /* Validate parameters */
    if (reading == NULL) {
        return -EINVAL;
    }

    if (!data->enabled) {
        LOG_WRN("Device not enabled");
        return MY_DRIVER_ERR_NOT_READY;
    }

    /* Wait for data ready signal */
    ret = k_sem_take(&data->data_ready_sem, timeout);
    if (ret == -EAGAIN) {
        LOG_DBG("Timeout waiting for data");
        return -ETIMEDOUT;
    }

    /* Copy reading to user buffer */
    k_sem_take(&data->lock, K_FOREVER);
    memcpy(reading, &data->last_reading, sizeof(*reading));
    k_sem_give(&data->lock);

    return 0;
}

/**
 * Write data to device
 */
static int my_driver_write_impl(const struct device *dev,
                                const uint8_t *buf,
                                size_t len)
{
    struct my_driver_data *data = dev->data;
    int ret;

    /* Validate parameters */
    if (buf == NULL || len == 0) {
        return -EINVAL;
    }

    if (len > MY_DRIVER_MAX_TRANSFER_SIZE) {
        LOG_ERR("Transfer too large: %zu", len);
        return -EINVAL;
    }

    /* Acquire lock */
    ret = k_sem_take(&data->lock, K_MSEC(1000));
    if (ret != 0) {
        return -EBUSY;
    }

    /* Perform write */
    ret = reg_write(dev, 0x10 /* DATA_REG */, buf, len);

    if (ret == 0) {
        data->stats.successful_transfers++;
        data->stats.bytes_transferred += len;
        ret = len;  /* Return bytes written on success */
    } else {
        data->stats.failed_transfers++;
        data->stats.last_error = ret;
    }

    data->stats.total_transfers++;

    k_sem_give(&data->lock);

    return ret;
}

/**
 * Set callback
 */
int my_driver_set_callback(const struct device *dev,
                           my_driver_callback_t callback,
                           void *user_data)
{
    struct my_driver_data *data = dev->data;

    k_sem_take(&data->lock, K_FOREVER);
    data->callback = callback;
    data->callback_user_data = user_data;
    k_sem_give(&data->lock);

    LOG_DBG("Callback %s", callback ? "registered" : "cleared");

    return 0;
}

/**
 * Get statistics
 */
int my_driver_get_stats(const struct device *dev, struct my_driver_stats *stats)
{
    struct my_driver_data *data = dev->data;

    if (stats == NULL) {
        return -EINVAL;
    }

    k_sem_take(&data->lock, K_FOREVER);
    memcpy(stats, &data->stats, sizeof(*stats));
    k_sem_give(&data->lock);

    return 0;
}

/**
 * Reset statistics
 */
int my_driver_reset_stats(const struct device *dev)
{
    struct my_driver_data *data = dev->data;

    k_sem_take(&data->lock, K_FOREVER);
    memset(&data->stats, 0, sizeof(data->stats));
    k_sem_give(&data->lock);

    LOG_DBG("Statistics reset");

    return 0;
}

/**
 * Enable device
 */
int my_driver_enable(const struct device *dev)
{
    struct my_driver_data *data = dev->data;
    const struct my_driver_config *config = dev->config;
    int ret = 0;

    k_sem_take(&data->lock, K_FOREVER);

    if (!data->enabled) {
        /* Enable hardware */
        ret = reg_write_byte(dev, 0x00 /* CTRL_REG */, 0x01 /* ENABLE */);

        if (ret == 0) {
            data->enabled = true;
            LOG_INF("Device enabled");
        }
    }

    k_sem_give(&data->lock);

    return ret;
}

/**
 * Disable device
 */
int my_driver_disable(const struct device *dev)
{
    struct my_driver_data *data = dev->data;
    int ret = 0;

    k_sem_take(&data->lock, K_FOREVER);

    if (data->enabled) {
        ret = reg_write_byte(dev, 0x00 /* CTRL_REG */, 0x00 /* DISABLE */);
        data->enabled = false;
        LOG_INF("Device disabled");
    }

    k_sem_give(&data->lock);

    return ret;
}

/**
 * =============================================================================
 * SECTION 4: DRIVER API STRUCTURE
 * =============================================================================
 *
 * This structure maps our implementation functions to the API
 * Applications use these through function pointers
 */
static const struct my_driver_api my_driver_api_impl = {
    .configure = my_driver_configure_impl,
    .read = my_driver_read_impl,
    .write = my_driver_write_impl,
};

/* Also provide direct function implementations for the public API */
int my_driver_configure(const struct device *dev,
                        const struct my_driver_user_config *config)
{
    return my_driver_configure_impl(dev, config);
}

int my_driver_read(const struct device *dev, struct my_driver_reading *reading)
{
    return my_driver_read_impl(dev, reading);
}

int my_driver_write(const struct device *dev, const uint8_t *buf, size_t len)
{
    return my_driver_write_impl(dev, buf, len);
}

/**
 * =============================================================================
 * SECTION 5: DEVICE INITIALIZATION
 * =============================================================================
 *
 * This function runs automatically at boot to initialize each device instance
 */

/**
 * Device initialization function
 *
 * WHY THE SIGNATURE: Zephyr expects: int init(const struct device *dev)
 * WHY CALLED AT BOOT: Registered via DEVICE_DT_INST_DEFINE
 *
 * INITIALIZATION ORDER:
 * 1. Validate configuration
 * 2. Initialize synchronization primitives
 * 3. Initialize hardware
 * 4. Configure interrupts (if used)
 * 5. Leave device in known state (usually disabled)
 */
static int my_driver_init(const struct device *dev)
{
    const struct my_driver_config *config = dev->config;
    struct my_driver_data *data = dev->data;
    int ret;

    LOG_INF("Initializing %s", dev->name);

    /* Step 1: Validate bus is ready */
    if (!device_is_ready(config->bus)) {
        LOG_ERR("Bus device not ready");
        return -ENODEV;
    }

    /* Step 2: Initialize synchronization primitives */
    k_sem_init(&data->lock, 1, 1);           /* Binary semaphore (mutex) */
    k_sem_init(&data->data_ready_sem, 0, 1); /* Starts empty */

    /* Step 3: Initialize work item for deferred processing */
    data->dev = dev;  /* Store back-reference */
    k_work_init(&data->process_work, process_work_handler);

    /* Step 4: Set default configuration */
    data->user_config.mode = MY_DRIVER_MODE_POLLING;
    data->user_config.speed_hz = 100000;
    data->user_config.timeout_ms = MY_DRIVER_DEFAULT_TIMEOUT_MS;

    /* Step 5: Initialize interrupt GPIO (if specified in DT) */
    if (config->interrupt_enabled && config->int_gpio.port != NULL) {
        if (!gpio_is_ready_dt(&config->int_gpio)) {
            LOG_ERR("Interrupt GPIO not ready");
            return -ENODEV;
        }

        /* Configure as input with interrupt on rising edge */
        ret = gpio_pin_configure_dt(&config->int_gpio, GPIO_INPUT);
        if (ret < 0) {
            LOG_ERR("Failed to configure interrupt GPIO: %d", ret);
            return ret;
        }

        ret = gpio_pin_interrupt_configure_dt(&config->int_gpio, GPIO_INT_EDGE_TO_ACTIVE);
        if (ret < 0) {
            LOG_ERR("Failed to configure interrupt: %d", ret);
            return ret;
        }

        /* Setup callback */
        gpio_init_callback(&data->int_gpio_cb, int_gpio_handler, BIT(config->int_gpio.pin));
        ret = gpio_add_callback(config->int_gpio.port, &data->int_gpio_cb);
        if (ret < 0) {
            LOG_ERR("Failed to add GPIO callback: %d", ret);
            return ret;
        }

        LOG_DBG("Interrupt configured on GPIO %d", config->int_gpio.pin);
    }

    /* Step 6: Verify device identity (read WHO_AM_I or similar) */
    uint8_t device_id;
    ret = reg_read(dev, 0x0F /* WHO_AM_I */, &device_id, 1);
    if (ret < 0) {
        LOG_ERR("Failed to read device ID: %d", ret);
        return ret;
    }

    if (device_id != 0xAB /* EXPECTED_ID */) {
        LOG_ERR("Unexpected device ID: 0x%02X (expected 0xAB)", device_id);
        return -ENODEV;
    }

    LOG_INF("Device ID: 0x%02X", device_id);

    /* Step 7: Configure device with defaults */
    /* ... hardware-specific initialization ... */

    LOG_INF("Initialization complete");

    return 0;
}

/**
 * =============================================================================
 * SECTION 6: DEVICE INSTANTIATION MACROS
 * =============================================================================
 *
 * This is where the magic happens - one driver code supports multiple instances
 */

/**
 * Macro to create config structure for one instance
 *
 * WHY MACRO: Same pattern repeated for each instance, but with different values
 *
 * DT_INST_* macros access Device Tree properties:
 * - DT_INST_BUS(inst) - Get parent bus device
 * - DT_INST_REG_ADDR(inst) - Get register address (I2C addr, SPI CS, etc.)
 * - DT_INST_PROP(inst, prop) - Get property value
 * - DT_INST_GPIO_CTLR(inst, prop) - Get GPIO controller for property
 */
#define MY_DRIVER_CONFIG_INIT(inst)                                         \
    static const struct my_driver_config my_driver_config_##inst = {        \
        .bus = DEVICE_DT_GET(DT_INST_BUS(inst)),                           \
        .addr = DT_INST_REG_ADDR(inst),                                    \
        .int_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, int_gpios, {0}),        \
        .sample_rate = DT_INST_PROP_OR(inst, sample_rate, 100),            \
        .resolution = DT_INST_PROP_OR(inst, resolution, 12),               \
        .interrupt_enabled = DT_INST_NODE_HAS_PROP(inst, int_gpios),       \
    };

/**
 * Macro to create data structure for one instance
 */
#define MY_DRIVER_DATA_INIT(inst)                                           \
    static struct my_driver_data my_driver_data_##inst;

/**
 * Macro to define and register one device instance
 *
 * DEVICE_DT_INST_DEFINE parameters:
 * 1. inst - Instance number (0, 1, 2...)
 * 2. init_fn - Initialization function
 * 3. pm_device - Power management device (NULL if not used)
 * 4. data - Pointer to runtime data structure
 * 5. config - Pointer to configuration structure
 * 6. level - Initialization level (POST_KERNEL, APPLICATION, etc.)
 * 7. priority - Init priority within level (lower = earlier)
 * 8. api - Pointer to API structure
 *
 * INIT LEVELS (in order):
 * - EARLY: Before kernel starts
 * - PRE_KERNEL_1: Very early, no kernel services
 * - PRE_KERNEL_2: Early, minimal kernel services
 * - POST_KERNEL: After kernel, most drivers here
 * - APPLICATION: After all drivers, for apps
 */
#define MY_DRIVER_DEFINE(inst)                                              \
    MY_DRIVER_CONFIG_INIT(inst)                                             \
    MY_DRIVER_DATA_INIT(inst)                                               \
    DEVICE_DT_INST_DEFINE(inst,                                             \
                          my_driver_init,                                    \
                          NULL, /* No PM */                                  \
                          &my_driver_data_##inst,                           \
                          &my_driver_config_##inst,                         \
                          POST_KERNEL,                                       \
                          CONFIG_MY_DRIVER_INIT_PRIORITY,                   \
                          &my_driver_api_impl);

/**
 * Instantiate driver for ALL matching Device Tree nodes
 *
 * DT_INST_FOREACH_STATUS_OKAY(fn) calls fn(0), fn(1), fn(2)...
 * for each DT node with:
 * - compatible = "my-vendor,my-device" (matches DT_DRV_COMPAT)
 * - status = "okay"
 *
 * WHY THIS PATTERN: Write driver once, it auto-instantiates for all devices
 */
DT_INST_FOREACH_STATUS_OKAY(MY_DRIVER_DEFINE)
```

---

## Component-by-Component Breakdown

### Why Each Component Exists

| Component | Purpose | Location | When Used |
|-----------|---------|----------|-----------|
| **Header File** | Public API contract | `include/` | Compile time - by applications |
| **Config Struct** | Hardware constants | ROM/Flash | Boot - never changes |
| **Data Struct** | Runtime state | RAM | Runtime - changes constantly |
| **API Struct** | Function dispatch | ROM | Runtime - function calls |
| **Init Function** | Setup hardware | Runs at boot | Once at startup |
| **DT Binding** | Describe properties | Build system | Compile time |
| **Kconfig** | Build options | Build system | Compile time |

### The Device Lifecycle

```
BOOT SEQUENCE:

1. ┌─────────────────────────────────────────┐
   │ Device Tree parsed at compile time      │
   │ - Nodes found with matching compatible  │
   │ - Properties extracted into config      │
   └─────────────────────────────────────────┘
                      │
                      ▼
2. ┌─────────────────────────────────────────┐
   │ DEVICE_DT_INST_DEFINE creates:          │
   │ - Config struct (const, in Flash)       │
   │ - Data struct (in RAM, zeroed)          │
   │ - Device struct linking them            │
   └─────────────────────────────────────────┘
                      │
                      ▼
3. ┌─────────────────────────────────────────┐
   │ Kernel boots, calls init functions      │
   │ in order: PRE_KERNEL → POST_KERNEL      │
   │                                         │
   │ Your init function:                     │
   │ - Validates config                      │
   │ - Initializes semaphores                │
   │ - Sets up hardware                      │
   │ - Configures interrupts                 │
   └─────────────────────────────────────────┘
                      │
                      ▼
4. ┌─────────────────────────────────────────┐
   │ Application runs:                       │
   │                                         │
   │ dev = DEVICE_DT_GET(...)               │
   │ if (device_is_ready(dev)) {             │
   │     my_driver_configure(dev, &cfg);     │
   │     my_driver_enable(dev);              │
   │     my_driver_read(dev, &data);         │
   │ }                                       │
   └─────────────────────────────────────────┘
```

---

## File Structure Template

```
my_driver/
├── CMakeLists.txt          # Build configuration
├── Kconfig                 # Configuration options
├── dts/
│   └── bindings/
│       └── my-vendor,my-device.yaml  # Device Tree binding
├── include/
│   └── my_driver.h         # Public API header
└── src/
    ├── my_driver.c         # Main implementation
    ├── my_driver_shell.c   # Shell commands (optional)
    └── my_driver_pm.c      # Power management (optional)
```

### CMakeLists.txt Template

```cmake
# SPDX-License-Identifier: Apache-2.0

# Only build if enabled in Kconfig
if(CONFIG_MY_DRIVER)
    # Add include directory to compiler search path
    zephyr_include_directories(include)

    # Always compile main driver
    zephyr_library_sources(
        src/my_driver.c
    )

    # Conditionally compile shell commands
    zephyr_library_sources_ifdef(CONFIG_MY_DRIVER_SHELL
        src/my_driver_shell.c
    )

    # Conditionally compile power management
    zephyr_library_sources_ifdef(CONFIG_PM_DEVICE
        src/my_driver_pm.c
    )
endif()
```

### Kconfig Template

```kconfig
# SPDX-License-Identifier: Apache-2.0

menuconfig MY_DRIVER
    bool "My Driver"
    default y
    depends on I2C || SPI  # Depends on bus driver
    help
      Enable support for My Device.

if MY_DRIVER

config MY_DRIVER_INIT_PRIORITY
    int "Init priority"
    default 90
    help
      Device initialization priority. Must be higher (later) than
      the bus driver priority (typically 80 for I2C/SPI).

config MY_DRIVER_LOG_LEVEL
    int "Log level"
    default 3
    range 0 4
    help
      Log level: 0=OFF, 1=ERR, 2=WRN, 3=INF, 4=DBG

config MY_DRIVER_SHELL
    bool "Shell commands"
    default y
    depends on SHELL
    help
      Enable shell commands for testing and debugging.

config MY_DRIVER_TRIGGER
    bool "Interrupt support"
    default y
    help
      Enable interrupt-based data ready notification.

endif # MY_DRIVER
```

### Device Tree Binding Template

```yaml
# SPDX-License-Identifier: Apache-2.0
# dts/bindings/my-vendor,my-device.yaml

description: |
  My Device driver for Zephyr.

  Example usage in Device Tree:

    &i2c0 {
        my_sensor: my-device@48 {
            compatible = "my-vendor,my-device";
            reg = <0x48>;
            int-gpios = <&gpio0 15 GPIO_ACTIVE_HIGH>;
            sample-rate = <100>;
            resolution = <12>;
        };
    };

compatible: "my-vendor,my-device"

include: [i2c-device.yaml]  # Inherit standard I2C properties

properties:
  int-gpios:
    type: phandle-array
    description: |
      GPIO connected to interrupt pin (optional).
      If not specified, polling mode is used.

  sample-rate:
    type: int
    default: 100
    description: Sample rate in Hz (1-1000)

  resolution:
    type: int
    default: 12
    enum: [8, 10, 12, 14, 16]
    description: ADC resolution in bits
```

---

## Step-by-Step Implementation Guide

### Step 1: Define Your API (Header File)

**Think about:**
- What operations does your device support?
- What data will applications read/write?
- What configuration options are needed?

### Step 2: Design Data Structures

**Config struct - ask yourself:**
- What hardware resources does this device need?
- What values come from Device Tree?
- What never changes at runtime?

**Data struct - ask yourself:**
- What changes during operation?
- What synchronization do I need?
- What state do I need to track?

### Step 3: Implement Core Functions

**Order of implementation:**
1. `reg_read()` / `reg_write()` - low-level bus access
2. `init()` - device initialization
3. `configure()` - runtime configuration
4. `enable()` / `disable()` - power control
5. `read()` / `write()` - data operations
6. Interrupt handling (if needed)

### Step 4: Create Device Tree Binding

**Define:**
- Compatible string
- Required properties (reg, bus, etc.)
- Optional properties (with defaults)
- Property constraints (enums, ranges)

### Step 5: Add Kconfig Options

**Typical options:**
- Enable/disable driver
- Init priority
- Log level
- Optional features (shell, triggers)

### Step 6: Test and Debug

**Use shell commands to:**
- Read/write registers manually
- Test configurations
- Monitor statistics

---

## Summary: The Essential Pattern

```c
/* 1. Define DT_DRV_COMPAT */
#define DT_DRV_COMPAT vendor_device

/* 2. Define config struct (const, from DT) */
struct driver_config { ... };

/* 3. Define data struct (mutable, runtime) */
struct driver_data { ... };

/* 4. Implement init function */
static int driver_init(const struct device *dev) { ... }

/* 5. Implement API functions */
static int driver_read(const struct device *dev, ...) { ... }

/* 6. Define API structure */
static const struct driver_api api = { .read = driver_read, ... };

/* 7. Create instantiation macros */
#define DRIVER_DEFINE(inst) \
    static const struct driver_config config_##inst = { ... }; \
    static struct driver_data data_##inst; \
    DEVICE_DT_INST_DEFINE(inst, driver_init, NULL, \
        &data_##inst, &config_##inst, POST_KERNEL, 90, &api);

/* 8. Instantiate for all DT nodes */
DT_INST_FOREACH_STATUS_OKAY(DRIVER_DEFINE)
```

This pattern ensures:
- ✅ Multiple device instances supported
- ✅ Configuration from Device Tree
- ✅ Proper initialization order
- ✅ Thread-safe operation
- ✅ Consistent API for applications
