# Zephyr Driver Design Principles

A comprehensive guide to designing robust, maintainable, and efficient drivers in Zephyr RTOS.

## Table of Contents

1. [Core Design Principles](#core-design-principles)
2. [The Separation of Concerns](#the-separation-of-concerns)
3. [Thread Safety Design](#thread-safety-design)
4. [Error Handling Strategy](#error-handling-strategy)
5. [Resource Management](#resource-management)
6. [API Design Guidelines](#api-design-guidelines)
7. [Performance Considerations](#performance-considerations)
8. [Common Patterns and Anti-Patterns](#common-patterns-and-anti-patterns)

---

## Core Design Principles

### Principle 1: Hardware Abstraction

**WHY**: Applications should not know (or care) about hardware details.

```
┌─────────────────────────────────────────────────────────────────┐
│ BAD: Application directly accesses hardware                     │
│                                                                 │
│   app.c:                                                        │
│     I2C_WRITE(0x68, 0x6B, 0x00);  // Wake up MPU6050           │
│     I2C_READ(0x68, 0x3B, data, 6); // Read accel               │
│                                                                 │
│   PROBLEM: If hardware changes, application must change         │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│ GOOD: Application uses driver API                               │
│                                                                 │
│   app.c:                                                        │
│     mpu6050_enable(dev);                                        │
│     mpu6050_get_accel(dev, &accel);                            │
│                                                                 │
│   BENEFIT: Hardware details hidden, easy to change              │
└─────────────────────────────────────────────────────────────────┘
```

**Implementation Pattern:**

```c
/* GOOD: Abstract hardware access */
static int read_register(const struct device *dev, uint8_t reg, uint8_t *val)
{
    const struct my_config *cfg = dev->config;

    /* Hardware details hidden here */
    return i2c_reg_read_byte(cfg->bus, cfg->addr, reg, val);
}

/* Application just sees this clean interface */
int my_driver_get_value(const struct device *dev, int32_t *value)
{
    uint8_t raw[2];
    int ret;

    ret = read_register(dev, REG_DATA_H, &raw[0]);
    if (ret < 0) return ret;

    ret = read_register(dev, REG_DATA_L, &raw[1]);
    if (ret < 0) return ret;

    *value = (raw[0] << 8) | raw[1];
    return 0;
}
```

---

### Principle 2: Instance Independence

**WHY**: Same driver code should support multiple identical devices.

```
┌─────────────────────────────────────────────────────────────────┐
│ SCENARIO: Two temperature sensors on same I2C bus               │
│                                                                 │
│   &i2c0 {                                                       │
│       temp_sensor_1: sensor@48 {                                │
│           compatible = "vendor,temp-sensor";                    │
│           reg = <0x48>;                                         │
│       };                                                        │
│       temp_sensor_2: sensor@49 {                                │
│           compatible = "vendor,temp-sensor";                    │
│           reg = <0x49>;                                         │
│       };                                                        │
│   };                                                            │
│                                                                 │
│ RESULT: Two independent device instances from same driver       │
└─────────────────────────────────────────────────────────────────┘
```

**Why This Works:**

```c
/* Each instance gets its own config (different I2C address) */
static const struct sensor_config config_0 = {
    .bus = &i2c0,
    .addr = 0x48,  /* From DT reg property */
};

static const struct sensor_config config_1 = {
    .bus = &i2c0,
    .addr = 0x49,  /* Different address */
};

/* Each instance gets its own data (separate state) */
static struct sensor_data data_0;  /* State for sensor 1 */
static struct sensor_data data_1;  /* State for sensor 2 */

/* Same driver code, different instances */
/* In application: */
temp_sensor_read(sensor_1_dev, &temp1);  /* Uses config_0, data_0 */
temp_sensor_read(sensor_2_dev, &temp2);  /* Uses config_1, data_1 */
```

**Critical Rule: NEVER use global state for instance data!**

```c
/* BAD: Global state breaks multi-instance */
static int current_temperature;  /* Which instance does this belong to? */

int bad_read_temp(const struct device *dev, int *temp)
{
    current_temperature = read_from_hw();  /* Overwrites for all instances! */
    *temp = current_temperature;
    return 0;
}

/* GOOD: Per-instance state */
int good_read_temp(const struct device *dev, int *temp)
{
    struct sensor_data *data = dev->data;  /* Instance-specific */
    data->last_reading = read_from_hw();
    *temp = data->last_reading;
    return 0;
}
```

---

### Principle 3: Configuration vs. Data Separation

**WHY**: Optimize memory usage - store constants in Flash, variables in RAM.

```
┌─────────────────────────────────────────────────────────────────┐
│ MEMORY MAP                                                       │
│                                                                  │
│ FLASH/ROM (large, cheap, read-only):                            │
│ ┌──────────────────────────────────────────┐                    │
│ │  struct my_config (const) {               │                    │
│ │      bus_device,     /* Never changes */  │                    │
│ │      slave_address,  /* From DT */        │                    │
│ │      gpio_pin,       /* From DT */        │                    │
│ │  }                                        │                    │
│ └──────────────────────────────────────────┘                    │
│                                                                  │
│ RAM (limited, expensive, read-write):                           │
│ ┌──────────────────────────────────────────┐                    │
│ │  struct my_data {                         │                    │
│ │      current_mode,   /* Changes */        │                    │
│ │      buffer[],       /* Changes */        │                    │
│ │      semaphore,      /* Kernel object */  │                    │
│ │  }                                        │                    │
│ └──────────────────────────────────────────┘                    │
└─────────────────────────────────────────────────────────────────┘
```

**Decision Matrix: Config vs. Data**

| Ask Yourself | Config (const) | Data (mutable) |
|--------------|----------------|----------------|
| Does it come from Device Tree? | ✓ | |
| Can it change at runtime? | | ✓ |
| Is it a hardware resource? | ✓ | |
| Is it a kernel object (sem, mutex)? | | ✓ |
| Is it a counter or statistic? | | ✓ |
| Is it an interrupt number? | ✓ | |
| Is it a callback pointer? | | ✓ |
| Is it a default setting? | ✓ | |
| Is it the current setting? | | ✓ |

---

## The Separation of Concerns

### Layer Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    YOUR DRIVER                                   │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │                    PUBLIC API LAYER                       │    │
│  │  Purpose: Clean interface for applications               │    │
│  │  - Parameter validation                                   │    │
│  │  - Error code translation                                 │    │
│  │  - Documentation entry point                              │    │
│  │                                                           │    │
│  │  int my_driver_read(dev, buf, len) {                     │    │
│  │      validate_params();                                   │    │
│  │      acquire_lock();                                      │    │
│  │      result = internal_read();                            │    │
│  │      release_lock();                                      │    │
│  │      return translate_error(result);                      │    │
│  │  }                                                        │    │
│  └─────────────────────────────────────────────────────────┘    │
│                            │                                     │
│                            ▼                                     │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │                   INTERNAL LOGIC LAYER                    │    │
│  │  Purpose: Device-specific algorithms                      │    │
│  │  - Data conversion                                        │    │
│  │  - State management                                       │    │
│  │  - Protocol handling                                      │    │
│  │                                                           │    │
│  │  static int internal_read() {                            │    │
│  │      raw_data = hw_read_register(DATA_REG);              │    │
│  │      return convert_to_units(raw_data);                  │    │
│  │  }                                                        │    │
│  └─────────────────────────────────────────────────────────┘    │
│                            │                                     │
│                            ▼                                     │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │                 HARDWARE ACCESS LAYER                     │    │
│  │  Purpose: Isolate bus communication                       │    │
│  │  - Register read/write                                    │    │
│  │  - Bus protocol specifics                                 │    │
│  │  - Hardware timing                                        │    │
│  │                                                           │    │
│  │  static int hw_read_register(reg) {                      │    │
│  │      return i2c_reg_read_byte(bus, addr, reg, &val);     │    │
│  │  }                                                        │    │
│  └─────────────────────────────────────────────────────────┘    │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### Why This Layering Matters

**Scenario: Change from I2C to SPI version of same sensor**

```c
/* Without layers - changes scattered everywhere */
int read_temperature_bad(const struct device *dev, int *temp)
{
    uint8_t data[2];

    /* I2C-specific code mixed with logic */
    i2c_burst_read(i2c_dev, 0x48, REG_TEMP, data, 2);  /* Change this */

    /* Conversion logic */
    *temp = ((data[0] << 8) | data[1]) / 16;

    return 0;
}

/* With layers - only HAL changes */

/* Hardware Access Layer - ONLY this changes */
static int hw_read_register(const struct device *dev, uint8_t reg,
                            uint8_t *data, size_t len)
{
    const struct config *cfg = dev->config;

#if defined(CONFIG_MY_DRIVER_I2C)
    return i2c_burst_read(cfg->bus, cfg->addr, reg, data, len);
#elif defined(CONFIG_MY_DRIVER_SPI)
    uint8_t tx = reg | 0x80;  /* Read bit for SPI */
    return spi_transceive(cfg->bus, &cfg->spi_cfg, &tx, 1, data, len);
#endif
}

/* Internal Logic Layer - unchanged */
static int convert_temperature(const uint8_t *raw)
{
    return ((raw[0] << 8) | raw[1]) / 16;
}

/* Public API Layer - unchanged */
int read_temperature(const struct device *dev, int *temp)
{
    uint8_t data[2];
    int ret;

    ret = hw_read_register(dev, REG_TEMP, data, 2);
    if (ret < 0) return ret;

    *temp = convert_temperature(data);
    return 0;
}
```

---

## Thread Safety Design

### Why Thread Safety Matters

```
┌─────────────────────────────────────────────────────────────────┐
│ SCENARIO: Two threads reading sensor simultaneously             │
│                                                                  │
│ Thread A                     Thread B                           │
│    │                            │                               │
│    ▼                            │                               │
│ read_sensor()                   │                               │
│   start_conversion()            │                               │
│    │                            ▼                               │
│    │ ◄──PREEMPTED──────  read_sensor()                         │
│    │                       start_conversion()  ← Overwrites!    │
│    │                       wait_for_data()                      │
│    │                       read_data() ◄── Gets Thread A's?     │
│    ▼                            │                               │
│ wait_for_data()                 │                               │
│ read_data() ◄── Wrong data!     │                               │
│                                                                  │
│ RESULT: Data corruption, race conditions                        │
└─────────────────────────────────────────────────────────────────┘
```

### Thread Safety Patterns

#### Pattern 1: Mutex Protection

```c
struct my_data {
    struct k_mutex lock;
    /* Protected data */
    uint8_t buffer[256];
    int current_value;
};

int my_driver_read(const struct device *dev, int *value)
{
    struct my_data *data = dev->data;
    int ret;

    /* Acquire mutex - blocks if another thread holds it */
    ret = k_mutex_lock(&data->lock, K_MSEC(1000));
    if (ret != 0) {
        LOG_ERR("Failed to acquire lock: %d", ret);
        return -EBUSY;
    }

    /* Critical section - only one thread at a time */
    ret = do_actual_read(dev, value);

    /* Always release, even on error */
    k_mutex_unlock(&data->lock);

    return ret;
}
```

#### Pattern 2: Binary Semaphore (Simple Lock)

```c
struct my_data {
    struct k_sem lock;  /* Init to 1,1 */
};

static int init(const struct device *dev)
{
    struct my_data *data = dev->data;

    /* Binary semaphore: initial=1 (available), max=1 */
    k_sem_init(&data->lock, 1, 1);

    return 0;
}

int my_driver_operation(const struct device *dev)
{
    struct my_data *data = dev->data;

    k_sem_take(&data->lock, K_FOREVER);
    /* ... critical section ... */
    k_sem_give(&data->lock);

    return 0;
}
```

#### Pattern 3: Counting Semaphore (Resource Pool)

```c
/* Example: Driver manages pool of DMA channels */
struct dma_driver_data {
    struct k_sem available_channels;  /* Count of free channels */
    uint8_t channel_used[4];          /* Which channels in use */
};

static int init(const struct device *dev)
{
    struct dma_driver_data *data = dev->data;

    /* 4 channels available initially */
    k_sem_init(&data->available_channels, 4, 4);

    return 0;
}

int allocate_channel(const struct device *dev)
{
    struct dma_driver_data *data = dev->data;

    /* Blocks if all 4 channels in use */
    if (k_sem_take(&data->available_channels, K_NO_WAIT) != 0) {
        return -EBUSY;  /* No channels available */
    }

    /* Find and mark a free channel */
    for (int i = 0; i < 4; i++) {
        if (!data->channel_used[i]) {
            data->channel_used[i] = 1;
            return i;
        }
    }

    /* Should not reach here */
    k_sem_give(&data->available_channels);
    return -ENOENT;
}

void release_channel(const struct device *dev, int channel)
{
    struct dma_driver_data *data = dev->data;

    data->channel_used[channel] = 0;
    k_sem_give(&data->available_channels);  /* Increment available count */
}
```

### ISR Safety: What You Can't Do

```c
/* ISR CONTEXT RULES:
 *
 * ❌ CANNOT:
 * - k_mutex_lock() - Mutexes can't be used in ISR
 * - k_sem_take() with non-zero timeout
 * - Any blocking call
 * - Allocate memory
 * - Do lengthy operations
 *
 * ✅ CAN:
 * - k_sem_give() - Always safe
 * - k_work_submit() - Defer to thread context
 * - k_fifo_put() - Non-blocking queue
 * - Set flags
 * - Quick register reads/writes
 */

/* ISR Handler - keep it FAST */
static void gpio_isr(const struct device *gpio_dev,
                     struct gpio_callback *cb,
                     uint32_t pins)
{
    struct my_data *data = CONTAINER_OF(cb, struct my_data, gpio_cb);

    /* DON'T do this in ISR - it's slow and might block */
    // i2c_read(bus, addr, reg, data, len);  ❌

    /* DO this - defer work to thread context */
    k_work_submit(&data->irq_work);  /* ✓ */

    /* OR signal a waiting thread */
    k_sem_give(&data->data_ready);   /* ✓ */
}

/* Deferred work runs in thread context */
static void irq_work_handler(struct k_work *work)
{
    struct my_data *data = CONTAINER_OF(work, struct my_data, irq_work);
    const struct device *dev = data->dev;

    /* Now we CAN do blocking operations */
    k_mutex_lock(&data->lock, K_FOREVER);
    i2c_read(...);  /* Safe in thread context */
    k_mutex_unlock(&data->lock);
}
```

---

## Error Handling Strategy

### Error Code Conventions

```c
/* Zephyr Error Convention:
 *
 *  0        = Success
 *  Positive = Success with info (bytes transferred, etc.)
 *  Negative = Error (use errno.h codes)
 *
 * Common error codes:
 *  -EINVAL   : Invalid argument
 *  -ENODEV   : Device not found / not ready
 *  -EBUSY    : Resource busy
 *  -EIO      : I/O error (hardware failure)
 *  -ETIMEDOUT: Operation timed out
 *  -ENODATA  : No data available
 *  -EAGAIN   : Try again (non-blocking, no data now)
 *  -ENOTSUP  : Operation not supported
 *  -ENOMEM   : Out of memory
 */

/* Example: Comprehensive error handling */
int my_driver_read(const struct device *dev, uint8_t *buf, size_t len)
{
    const struct my_config *cfg = dev->config;
    struct my_data *data = dev->data;
    int ret;

    /* 1. Validate parameters */
    if (buf == NULL) {
        LOG_ERR("NULL buffer");
        return -EINVAL;
    }
    if (len == 0 || len > MAX_READ_SIZE) {
        LOG_ERR("Invalid length: %zu", len);
        return -EINVAL;
    }

    /* 2. Check device state */
    if (!data->initialized) {
        LOG_WRN("Device not initialized");
        return -ENODEV;
    }

    /* 3. Try to acquire resource */
    ret = k_sem_take(&data->lock, K_MSEC(100));
    if (ret == -EAGAIN) {
        LOG_DBG("Lock timeout");
        return -EBUSY;
    }

    /* 4. Perform operation with retry */
    for (int attempt = 0; attempt < 3; attempt++) {
        ret = i2c_read(cfg->bus, buf, len, cfg->addr);
        if (ret == 0) {
            break;  /* Success */
        }
        LOG_WRN("Read attempt %d failed: %d", attempt + 1, ret);
        k_sleep(K_MSEC(10));  /* Brief delay before retry */
    }

    k_sem_give(&data->lock);

    /* 5. Translate hardware error to appropriate code */
    if (ret < 0) {
        LOG_ERR("Read failed after retries: %d", ret);
        data->stats.error_count++;
        return -EIO;
    }

    data->stats.success_count++;
    return len;  /* Return bytes read on success */
}
```

### Error Propagation Pattern

```c
/* Propagate errors up the call stack */
static int low_level_read(const struct device *dev, uint8_t reg, uint8_t *val)
{
    int ret = i2c_reg_read_byte(cfg->bus, cfg->addr, reg, val);
    if (ret < 0) {
        LOG_DBG("Register 0x%02X read failed: %d", reg, ret);
        return ret;  /* Propagate error */
    }
    return 0;
}

static int mid_level_read_data(const struct device *dev, struct reading *r)
{
    int ret;

    ret = low_level_read(dev, REG_DATA_H, &r->high);
    if (ret < 0) return ret;  /* Propagate */

    ret = low_level_read(dev, REG_DATA_L, &r->low);
    if (ret < 0) return ret;  /* Propagate */

    return 0;
}

/* Public API: Add context to errors */
int my_driver_get_reading(const struct device *dev, int32_t *value)
{
    struct reading raw;
    int ret;

    ret = mid_level_read_data(dev, &raw);
    if (ret < 0) {
        LOG_ERR("Failed to read sensor data: %d", ret);
        return ret;  /* Propagate */
    }

    *value = (raw.high << 8) | raw.low;
    return 0;
}
```

---

## Resource Management

### Initialization Order

```c
/*
 * INIT LEVELS (executed in this order):
 *
 * 1. EARLY       - Before kernel, absolute minimum
 * 2. PRE_KERNEL_1 - Early kernel init
 * 3. PRE_KERNEL_2 - Basic kernel services
 * 4. POST_KERNEL  - After kernel, most drivers here
 * 5. APPLICATION  - After all drivers
 *
 * WITHIN each level, lower priority number = earlier init
 *
 * Example initialization order:
 *   GPIO       : POST_KERNEL, priority 40
 *   I2C/SPI    : POST_KERNEL, priority 50
 *   Sensor     : POST_KERNEL, priority 60 (needs I2C first)
 *   Application: APPLICATION, priority 99
 */

/* Sensor driver depends on I2C - init after I2C */
DEVICE_DT_INST_DEFINE(0,
    sensor_init,
    NULL,
    &sensor_data,
    &sensor_config,
    POST_KERNEL,
    CONFIG_SENSOR_INIT_PRIORITY,  /* Default: 90, after I2C's 50 */
    &sensor_api);

/* In Kconfig: */
config SENSOR_INIT_PRIORITY
    int "Sensor init priority"
    default 90
    help
      Must be higher (later) than I2C init priority (50).
```

### Checking Dependencies

```c
static int my_driver_init(const struct device *dev)
{
    const struct my_config *cfg = dev->config;

    /* Check bus device is ready */
    if (!device_is_ready(cfg->bus)) {
        LOG_ERR("Bus device %s not ready", cfg->bus->name);
        return -ENODEV;
    }

    /* Check GPIO is ready (if used) */
    if (cfg->int_gpio.port != NULL) {
        if (!gpio_is_ready_dt(&cfg->int_gpio)) {
            LOG_ERR("Interrupt GPIO not ready");
            return -ENODEV;
        }
    }

    /* Check optional reset GPIO */
    if (cfg->reset_gpio.port != NULL) {
        if (!gpio_is_ready_dt(&cfg->reset_gpio)) {
            LOG_ERR("Reset GPIO not ready");
            return -ENODEV;
        }

        /* Perform hardware reset */
        gpio_pin_configure_dt(&cfg->reset_gpio, GPIO_OUTPUT_ACTIVE);
        k_sleep(K_MSEC(10));
        gpio_pin_set_dt(&cfg->reset_gpio, 0);  /* Release reset */
        k_sleep(K_MSEC(50));  /* Wait for device to boot */
    }

    return 0;
}
```

---

## API Design Guidelines

### Naming Conventions

```c
/*
 * NAMING PATTERN: <subsystem>_<action>_<object>
 *
 * Public functions: Use driver prefix
 *   my_driver_read()
 *   my_driver_configure()
 *   my_driver_get_status()
 *
 * Internal functions: Use static, descriptive names
 *   static int read_register()
 *   static void process_data()
 *   static int configure_hardware()
 *
 * Macros: UPPER_CASE
 *   MY_DRIVER_MAX_SIZE
 *   MY_DRIVER_DEFAULT_TIMEOUT
 *
 * Structs: my_driver_<purpose>
 *   struct my_driver_config
 *   struct my_driver_data
 *   struct my_driver_reading
 *
 * Enums: my_driver_<category>
 *   enum my_driver_mode { MY_DRIVER_MODE_POLLING, ... }
 */
```

### Function Signature Patterns

```c
/*
 * PATTERN 1: Simple read/write
 *
 * Input: const pointers
 * Output: non-const pointers
 * Return: 0 on success, negative error
 */
int my_driver_write(const struct device *dev,
                    const uint8_t *buf,    /* Input: const */
                    size_t len);           /* Return: error or bytes */

int my_driver_read(const struct device *dev,
                   uint8_t *buf,           /* Output: non-const */
                   size_t len);

/*
 * PATTERN 2: Get structured data
 *
 * Output parameter is pointer to user-provided struct
 */
int my_driver_get_reading(const struct device *dev,
                          struct my_reading *reading);  /* Output */

/*
 * PATTERN 3: Configuration
 *
 * Input is const pointer to config struct
 */
int my_driver_configure(const struct device *dev,
                        const struct my_config *config);  /* Input: const */

/*
 * PATTERN 4: Callback registration
 *
 * Callback + user_data pair
 */
int my_driver_set_callback(const struct device *dev,
                           my_callback_t callback,
                           void *user_data);

/*
 * PATTERN 5: Async operation
 *
 * Non-blocking, completion via callback
 */
int my_driver_async_read(const struct device *dev,
                         uint8_t *buf,
                         size_t len,
                         my_callback_t callback,
                         void *user_data);

/*
 * PATTERN 6: Timeout variant
 *
 * Blocking with configurable timeout
 */
int my_driver_read_timeout(const struct device *dev,
                           uint8_t *buf,
                           size_t len,
                           k_timeout_t timeout);
```

---

## Performance Considerations

### Minimize ISR Time

```c
/* BAD: Too much work in ISR */
void bad_isr(void *arg)
{
    /* Reading I2C in ISR - SLOW! */
    i2c_read(bus, buffer, 32, addr);

    /* Processing data in ISR - SLOW! */
    for (int i = 0; i < 32; i++) {
        result += buffer[i] * coefficient[i];
    }

    /* Calling callback that might do more work */
    user_callback(result);
}

/* GOOD: Minimal ISR, defer work */
void good_isr(void *arg)
{
    struct my_data *data = arg;

    /* Just signal and return - FAST */
    k_sem_give(&data->data_ready);
    /* OR */
    k_work_submit(&data->process_work);
}
```

### Efficient Data Structures

```c
/* For ISR-to-thread communication, use lock-free structures */

/* Ring buffer - good for streaming data */
#include <zephyr/sys/ring_buffer.h>

RING_BUF_DECLARE(my_ring_buf, 256);

void isr_handler(void)
{
    uint8_t data = read_hw_register();
    ring_buf_put(&my_ring_buf, &data, 1);  /* Non-blocking */
}

void thread_handler(void)
{
    uint8_t data;
    while (ring_buf_get(&my_ring_buf, &data, 1) > 0) {
        process(data);
    }
}

/* FIFO - good for message passing */
K_FIFO_DEFINE(my_fifo);

struct my_message {
    void *fifo_reserved;  /* Required by k_fifo */
    uint8_t data[32];
};

void isr_handler(void)
{
    struct my_message *msg = k_malloc(sizeof(*msg));
    if (msg) {
        memcpy(msg->data, hw_buffer, 32);
        k_fifo_put(&my_fifo, msg);
    }
}
```

### Avoid Busy Waiting

```c
/* BAD: Busy wait wastes CPU */
int bad_wait_for_ready(const struct device *dev)
{
    while (!(read_status() & READY_BIT)) {
        /* Spinning, burning CPU cycles */
    }
    return 0;
}

/* BETTER: Poll with sleep */
int better_wait_for_ready(const struct device *dev)
{
    for (int i = 0; i < 100; i++) {
        if (read_status() & READY_BIT) {
            return 0;
        }
        k_sleep(K_MSEC(1));  /* Let other threads run */
    }
    return -ETIMEDOUT;
}

/* BEST: Use interrupts */
int best_wait_for_ready(const struct device *dev)
{
    struct my_data *data = dev->data;

    /* Hardware will signal via interrupt */
    int ret = k_sem_take(&data->ready_sem, K_MSEC(100));
    return (ret == 0) ? 0 : -ETIMEDOUT;
}
```

---

## Common Patterns and Anti-Patterns

### Anti-Pattern: Global Variables

```c
/* ❌ BAD: Global state */
static const struct device *my_device;  /* Which instance? */
static int last_reading;                 /* Shared state! */

void init_my_driver(void)
{
    my_device = DEVICE_DT_GET(DT_NODELABEL(sensor));
}

/* ✓ GOOD: Instance-based */
struct my_data {
    int last_reading;  /* Per-instance */
};

/* Device pointer passed to every function */
int my_driver_read(const struct device *dev, int *value)
{
    struct my_data *data = dev->data;
    /* Access instance-specific state */
}
```

### Anti-Pattern: Magic Numbers

```c
/* ❌ BAD: Magic numbers */
int configure_sensor(const struct device *dev)
{
    write_reg(dev, 0x1A, 0x03);  /* What does this do? */
    write_reg(dev, 0x1B, 0x18);  /* No idea! */
    write_reg(dev, 0x6B, 0x01);
    return 0;
}

/* ✓ GOOD: Named constants */
#define REG_CONFIG      0x1A
#define REG_GYRO_CONFIG 0x1B
#define REG_PWR_MGMT    0x6B

#define CONFIG_DLPF_44HZ    0x03
#define GYRO_FS_2000DPS     0x18
#define PWR_CLKSEL_PLL      0x01

int configure_sensor(const struct device *dev)
{
    write_reg(dev, REG_CONFIG, CONFIG_DLPF_44HZ);
    write_reg(dev, REG_GYRO_CONFIG, GYRO_FS_2000DPS);
    write_reg(dev, REG_PWR_MGMT, PWR_CLKSEL_PLL);
    return 0;
}
```

### Anti-Pattern: Ignoring Errors

```c
/* ❌ BAD: Ignoring return values */
void bad_init(const struct device *dev)
{
    i2c_write(bus, data, len, addr);      /* Might fail! */
    k_sem_init(&sem, 0, 1);
    gpio_pin_configure(gpio, pin, flags); /* Might fail! */
}

/* ✓ GOOD: Check every return value */
int good_init(const struct device *dev)
{
    int ret;

    ret = i2c_write(bus, data, len, addr);
    if (ret < 0) {
        LOG_ERR("I2C write failed: %d", ret);
        return ret;
    }

    k_sem_init(&sem, 0, 1);  /* Can't fail */

    ret = gpio_pin_configure(gpio, pin, flags);
    if (ret < 0) {
        LOG_ERR("GPIO configure failed: %d", ret);
        return ret;
    }

    return 0;
}
```

### Pattern: Device Validation Macro

```c
/* Helper macro to validate device at function entry */
#define VALIDATE_DEVICE(dev)                         \
    do {                                             \
        if ((dev) == NULL) {                         \
            LOG_ERR("NULL device");                  \
            return -EINVAL;                          \
        }                                            \
        if (!device_is_ready(dev)) {                 \
            LOG_ERR("Device not ready: %s",          \
                    (dev)->name);                    \
            return -ENODEV;                          \
        }                                            \
    } while (0)

int my_driver_read(const struct device *dev, int *value)
{
    VALIDATE_DEVICE(dev);

    if (value == NULL) {
        return -EINVAL;
    }

    /* Proceed with operation */
    return 0;
}
```

---

## Summary: Design Checklist

Before shipping your driver, verify:

- [ ] **Abstraction**: Hardware details hidden from applications
- [ ] **Multi-instance**: No global state, all data in per-instance structs
- [ ] **Config/Data split**: Constants in config (ROM), variables in data (RAM)
- [ ] **Thread safety**: Proper locking for shared resources
- [ ] **ISR safety**: No blocking calls in interrupt context
- [ ] **Error handling**: All errors checked and propagated
- [ ] **Init order**: Dependencies checked, proper init priority
- [ ] **API consistency**: Naming conventions, signature patterns
- [ ] **Performance**: No busy waits, minimal ISR time
- [ ] **Documentation**: Clear comments, doxygen for public API
