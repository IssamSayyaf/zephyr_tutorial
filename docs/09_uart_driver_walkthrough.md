# UART Driver Walkthrough - Complete Code Explanation

This document provides a line-by-line explanation of the UART Advanced driver, explaining the WHY and HOW of every component.

## Table of Contents

1. [Overview](#overview)
2. [Header File Analysis](#header-file-analysis)
3. [Source File Analysis](#source-file-analysis)
4. [Device Tree Binding Explained](#device-tree-binding-explained)
5. [How Data Flows](#how-data-flows)
6. [Key Design Decisions](#key-design-decisions)

---

## Overview

### What This Driver Does

The UART Advanced driver provides:
- **Polling mode**: Simple, blocking TX/RX
- **Interrupt mode**: Non-blocking with callbacks
- **Async mode**: DMA-based for high throughput
- **Ring buffers**: Efficient data buffering
- **Flow control**: Hardware RTS/CTS support

### File Structure

```
uart_advanced/
├── include/
│   └── uart_advanced.h     ← Public API (what apps see)
├── src/
│   ├── uart_advanced.c     ← Main implementation
│   └── uart_advanced_shell.c ← Debug commands
├── dts/bindings/
│   └── custom,uart-advanced.yaml ← Device Tree schema
├── CMakeLists.txt          ← Build configuration
└── Kconfig                 ← Configuration options
```

---

## Header File Analysis

### File: `include/uart_advanced.h`

```c
/**
 * File Guard
 *
 * WHY: Prevents multiple inclusion. Without this, if two .c files
 * both #include "uart_advanced.h", you'd get duplicate definitions.
 */
#ifndef UART_ADVANCED_H_
#define UART_ADVANCED_H_
```

```c
/**
 * C++ Compatibility
 *
 * WHY: C++ mangles function names (adds type info). extern "C" tells
 * C++ compiler to use C linkage, making the library usable from C++.
 */
#ifdef __cplusplus
extern "C" {
#endif
```

```c
/**
 * Required Headers
 *
 * WHY each include:
 * - device.h: For 'struct device' type
 * - kernel.h: For k_timeout_t, semaphores
 * - stdint.h: For uint8_t, uint32_t, etc.
 * - stdbool.h: For 'bool' type
 */
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>
```

### Operating Mode Enum

```c
/**
 * Operating Modes Explained
 *
 * POLLING MODE:
 *   CPU actively waits for each byte. Simple but wastes CPU.
 *   Use when: Low data rate, simplicity preferred
 *
 *   CPU: ████████░░░░████████░░░░  (busy waiting)
 *
 * INTERRUPT MODE:
 *   Hardware signals when byte ready. CPU does other work.
 *   Use when: Moderate data rate, need responsive system
 *
 *   CPU: ░░░░▓░░░░░▓░░░░░▓░░░░░  (brief ISR handling)
 *
 * ASYNC/DMA MODE:
 *   DMA controller moves data. CPU only notified on completion.
 *   Use when: High data rate, maximum CPU efficiency
 *
 *   CPU: ░░░░░░░░░░░░░░░░░░░▓░░  (only completion handler)
 *   DMA: ████████████████████░░  (handles transfer)
 */
enum uart_advanced_mode {
    UART_ADV_MODE_POLLING   = 0,  /* Blocking operations */
    UART_ADV_MODE_INTERRUPT = 1,  /* ISR-driven */
    UART_ADV_MODE_ASYNC     = 2,  /* DMA-driven */
};
```

### Configuration Structure

```c
/**
 * User Configuration Structure
 *
 * WHY A SEPARATE STRUCT: Bundles related settings together.
 * Easier to pass around than individual parameters.
 * Easier to extend later (add new fields without changing API).
 */
struct uart_advanced_config {
    /**
     * Baud Rate
     *
     * WHY UINT32: Supports up to 4+ Gbaud (more than any UART needs)
     * Common values: 9600, 115200, 921600, 1000000
     *
     * HOW IT WORKS: UART timing is derived from this value.
     * bit_time = 1 / baud_rate
     * At 115200 baud: bit_time = 8.68 microseconds
     */
    uint32_t baudrate;

    /**
     * Data Bits
     *
     * WHY CONFIGURABLE: Different protocols use different sizes.
     * Most common: 8 bits (one byte per transfer)
     * Legacy systems: 7 bits (ASCII only)
     */
    uint8_t data_bits;

    /**
     * Parity
     *
     * WHY: Error detection. Extra bit indicates if data has odd/even 1s.
     * NONE: No parity (most common, relies on higher-level checksums)
     * ODD/EVEN: Simple error detection (catches single-bit errors)
     */
    enum uart_advanced_parity parity;

    /**
     * Stop Bits
     *
     * WHY: Guarantees line returns to idle between bytes.
     * 1 bit: Faster, most common
     * 2 bits: More reliable on noisy lines, legacy equipment
     */
    enum uart_advanced_stop_bits stop_bits;

    /**
     * Flow Control
     *
     * WHY: Prevents buffer overflow when receiver can't keep up.
     *
     * NONE: No flow control - sender always transmits
     *   Risk: Data loss if receiver buffer fills
     *
     * RTS/CTS (Hardware flow control):
     *   RTS (Request To Send): Receiver pulls LOW when ready
     *   CTS (Clear To Send): Sender checks before transmitting
     *
     *   Receiver buffer filling:
     *   [████████░░] ← RTS HIGH (don't send!)
     *   Receiver buffer draining:
     *   [████░░░░░░] ← RTS LOW (ok to send)
     */
    enum uart_advanced_flow_ctrl flow_control;

    /**
     * Operating Mode
     *
     * WHY: Different use cases need different tradeoffs
     * See enum explanation above
     */
    enum uart_advanced_mode mode;
};
```

### Callback Function Type

```c
/**
 * Callback Function Signature
 *
 * WHY CALLBACKS: Asynchronous notification without polling.
 * Application registers function, driver calls it on events.
 *
 * Parameters:
 *   dev:       Which UART device (supports multiple UARTs)
 *   event:     What happened (RX ready, TX complete, error)
 *   user_data: Application context (avoids global variables)
 *
 * IMPORTANT: May be called from ISR context!
 *   - Keep it SHORT (< 10us ideally)
 *   - Don't call blocking functions
 *   - Don't do heavy processing
 *   - Use k_work_submit() for complex handling
 */
typedef void (*uart_advanced_callback_t)(const struct device *dev,
                                          enum uart_advanced_event event,
                                          void *user_data);
```

### Public API Functions

```c
/**
 * Configure UART
 *
 * WHEN TO CALL: After getting device, before any TX/RX
 *
 * WHY SEPARATE FROM INIT: Init happens at boot with defaults.
 * Configure allows runtime changes (e.g., auto-baud detection).
 *
 * @param dev    Device from DEVICE_DT_GET()
 * @param config Settings to apply
 * @return 0 success, negative error
 *
 * THREAD SAFETY: Safe to call from any thread
 * ISR SAFETY: NOT safe from ISR (may block)
 */
int uart_advanced_configure(const struct device *dev,
                            const struct uart_advanced_config *config);
```

```c
/**
 * Transmit Data (Blocking)
 *
 * BEHAVIOR BY MODE:
 *   POLLING: Waits for each byte to send
 *   INTERRUPT: Queues data, blocks until all sent
 *   ASYNC: Queues data, blocks until DMA complete
 *
 * @param dev  Device instance
 * @param buf  Data to transmit
 * @param len  Number of bytes
 *
 * @return Bytes sent (>= 0) or negative error
 *
 * WHY RETURN BYTES: Partial sends possible on timeout/error.
 * Application can retry remaining data.
 */
int uart_advanced_tx(const struct device *dev,
                     const uint8_t *buf,
                     size_t len);
```

```c
/**
 * Receive Data (Blocking with Timeout)
 *
 * WHY TIMEOUT: Prevents infinite block if no data arrives.
 *
 * @param dev     Device instance
 * @param buf     Buffer for received data
 * @param len     Buffer size (max bytes to receive)
 * @param timeout How long to wait (K_FOREVER, K_NO_WAIT, K_MSEC(n))
 *
 * @return Bytes received (>= 0) or negative error
 *
 * USAGE PATTERNS:
 *   Non-blocking check: uart_advanced_rx(dev, buf, len, K_NO_WAIT);
 *   Wait up to 1 sec:   uart_advanced_rx(dev, buf, len, K_MSEC(1000));
 *   Wait forever:       uart_advanced_rx(dev, buf, len, K_FOREVER);
 */
int uart_advanced_rx(const struct device *dev,
                     uint8_t *buf,
                     size_t len,
                     k_timeout_t timeout);
```

---

## Source File Analysis

### File: `src/uart_advanced.c`

```c
/**
 * DT_DRV_COMPAT Definition
 *
 * CRITICAL: Must be first, before ANY includes!
 *
 * WHY: This macro tells DT_INST_* macros which devices to find.
 * Format: vendor_device (underscores replace special chars)
 *
 * Device Tree: compatible = "custom,uart-advanced"
 * DT_DRV_COMPAT: custom_uart_advanced
 */
#define DT_DRV_COMPAT custom_uart_advanced
```

```c
/**
 * Includes - Order Matters!
 *
 * 1. Zephyr system headers first
 * 2. Zephyr driver headers
 * 3. Our public header last
 *
 * WHY ORDER: Some headers depend on macros defined in others.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/logging/log.h>

#include "uart_advanced.h"
```

### Logging Setup

```c
/**
 * Logging Module Registration
 *
 * LOG_MODULE_REGISTER(name, default_level)
 *
 * name: Appears in log output: [uart_advanced] Message
 * default_level: Used if not overridden in prj.conf
 *
 * Levels:
 *   0 = LOG_LEVEL_NONE  - No logging
 *   1 = LOG_LEVEL_ERR   - Errors only
 *   2 = LOG_LEVEL_WRN   - Warnings and errors
 *   3 = LOG_LEVEL_INF   - Info, warnings, errors
 *   4 = LOG_LEVEL_DBG   - Everything
 *
 * Runtime control via prj.conf:
 *   CONFIG_UART_ADVANCED_LOG_LEVEL=4
 */
LOG_MODULE_REGISTER(uart_advanced, CONFIG_UART_ADVANCED_LOG_LEVEL);
```

### Configuration Structure (Private)

```c
/**
 * Hardware Configuration - CONST, lives in FLASH
 *
 * These values come from Device Tree and NEVER change.
 * Keeping them const saves RAM and enables compiler optimizations.
 */
struct uart_advanced_config_hw {
    /**
     * Underlying UART Device
     *
     * WHY: We wrap Zephyr's UART driver, adding features.
     * The 'uart' pointer is the real hardware driver.
     *
     * Example: If DT says uart = <&uart0>;
     * This becomes: .uart = DEVICE_DT_GET(DT_NODELABEL(uart0))
     */
    const struct device *uart;

    /**
     * Default Settings from Device Tree
     *
     * WHY: Sensible defaults without explicit configure() call.
     * Application can override via uart_advanced_configure().
     */
    uint32_t default_baudrate;
    uint8_t default_data_bits;
    uint8_t default_parity;
    uint8_t default_stop_bits;

    /**
     * Buffer Sizes
     *
     * WHY CONFIGURABLE: Different apps need different buffer sizes.
     * Large buffers: Handle bursts, use more RAM
     * Small buffers: Save RAM, may lose data on bursts
     */
    size_t tx_buf_size;
    size_t rx_buf_size;
};
```

### Runtime Data Structure

```c
/**
 * Runtime Data - MUTABLE, lives in RAM
 *
 * Everything that changes during operation goes here.
 */
struct uart_advanced_data {
    /**
     * Synchronization Primitives
     */

    /**
     * TX Mutex
     *
     * WHY MUTEX (not semaphore): Mutexes support priority inheritance.
     * If low-priority thread holds mutex, and high-priority thread
     * waits, low-priority temporarily gets high priority to avoid
     * priority inversion.
     *
     * USAGE: Lock before TX operation, unlock after.
     */
    struct k_mutex tx_lock;

    /**
     * RX Semaphore
     *
     * WHY SEMAPHORE: Signaling "data available" from ISR.
     * ISR can give() semaphore (non-blocking).
     * Thread can take() with timeout (blocking).
     *
     * Initial count: 0 (no data available yet)
     * Max count: 1 (binary semaphore - just signals "has data")
     */
    struct k_sem rx_sem;

    /**
     * TX Complete Semaphore
     *
     * WHY: For blocking TX operations.
     * tx() gives this after all data sent.
     * Waiting thread takes when complete.
     */
    struct k_sem tx_done;

    /**
     * Ring Buffers
     *
     * WHY RING BUFFER: Efficient FIFO for producer-consumer pattern.
     *
     * ISR (producer): ring_buf_put() - adds data
     * Thread (consumer): ring_buf_get() - removes data
     *
     * Properties:
     * - Lock-free (ISR-safe)
     * - Constant time operations
     * - Wrap-around handled automatically
     *
     * Visualization:
     *   [H░░░░░░░░T░░░░░░]
     *    ↑        ↑
     *    Head     Tail
     *    (read)   (write)
     */
    struct ring_buf tx_ring;
    struct ring_buf rx_ring;

    /**
     * Ring Buffer Storage
     *
     * WHY SEPARATE: ring_buf struct is metadata,
     * actual data storage is here.
     * Size comes from Device Tree via config.
     */
    uint8_t *tx_buf;
    uint8_t *rx_buf;

    /**
     * Current State
     */
    enum uart_advanced_mode current_mode;
    bool enabled;

    /**
     * Callback Storage
     *
     * WHY STORE: Application registers once, driver calls on events.
     * user_data: App-provided context (avoids globals).
     */
    uart_advanced_callback_t callback;
    void *callback_user_data;

    /**
     * Statistics
     *
     * WHY: Debugging, monitoring, performance analysis.
     * Increment atomically if possible, or under lock.
     */
    uint32_t tx_bytes;
    uint32_t rx_bytes;
    uint32_t errors;
};
```

### Interrupt Handler

```c
/**
 * UART Interrupt Handler
 *
 * CALLED BY: Hardware when UART event occurs
 * CONTEXT: ISR (Interrupt Service Routine)
 *
 * CRITICAL RULES:
 * 1. Keep it SHORT - ISRs block all lower-priority interrupts
 * 2. No blocking calls - k_mutex_lock(), k_sleep() forbidden
 * 3. No printf/LOG_* (in default config) - use LOG_INST_* if needed
 * 4. Minimize work - just get data, signal threads
 */
static void uart_isr_handler(const struct device *uart_dev, void *user_data)
{
    /* Get our device from user_data */
    const struct device *dev = user_data;
    struct uart_advanced_data *data = dev->data;

    /**
     * Process while UART has data/events
     *
     * WHY WHILE LOOP: Single interrupt may signal multiple events.
     * UART might have received multiple bytes by the time ISR runs.
     */
    while (uart_irq_update(uart_dev) && uart_irq_is_pending(uart_dev)) {

        /**
         * RX Ready - Data received from external device
         */
        if (uart_irq_rx_ready(uart_dev)) {
            uint8_t byte;

            /**
             * Read all available bytes
             *
             * WHY LOOP: UART may have small FIFO (typically 1-16 bytes).
             * Reading one byte might leave more in FIFO.
             */
            while (uart_fifo_read(uart_dev, &byte, 1) > 0) {
                /**
                 * Put in ring buffer
                 *
                 * WHY RING BUFFER: ISR must not block.
                 * Ring buffer put is O(1), non-blocking.
                 *
                 * WHAT IF FULL: Data lost! Size buffer appropriately
                 * or use flow control.
                 */
                ring_buf_put(&data->rx_ring, &byte, 1);
            }

            /**
             * Signal waiting thread
             *
             * k_sem_give() is ISR-safe (non-blocking).
             * If thread is waiting in k_sem_take(), it wakes up.
             * If no one waiting, semaphore count increments.
             */
            k_sem_give(&data->rx_sem);

            /**
             * Call user callback if registered
             *
             * WARNING: Callback runs in ISR context!
             * User must keep it short.
             */
            if (data->callback) {
                data->callback(dev, UART_ADV_EVT_RX_READY, data->callback_user_data);
            }
        }

        /**
         * TX Ready - Hardware can accept more data
         */
        if (uart_irq_tx_ready(uart_dev)) {
            uint8_t byte;

            /**
             * Check if we have data to send
             */
            if (ring_buf_get(&data->tx_ring, &byte, 1) > 0) {
                /* Send the byte */
                uart_fifo_fill(uart_dev, &byte, 1);
                data->tx_bytes++;
            } else {
                /**
                 * No more data - disable TX interrupt
                 *
                 * WHY DISABLE: TX-ready interrupt fires continuously
                 * when FIFO has space. Without data to send, we'd
                 * get infinite interrupts!
                 */
                uart_irq_tx_disable(uart_dev);

                /* Signal TX complete */
                k_sem_give(&data->tx_done);

                if (data->callback) {
                    data->callback(dev, UART_ADV_EVT_TX_DONE, data->callback_user_data);
                }
            }
        }
    }
}
```

### Transmit Function Implementation

```c
/**
 * Transmit Data - The Main TX Function
 *
 * This function demonstrates several important patterns:
 * 1. Input validation
 * 2. Mode-specific behavior
 * 3. Thread safety
 * 4. Error handling
 */
int uart_advanced_tx(const struct device *dev,
                     const uint8_t *buf,
                     size_t len)
{
    const struct uart_advanced_config_hw *config = dev->config;
    struct uart_advanced_data *data = dev->data;
    int ret;

    /**
     * Step 1: Input Validation
     *
     * WHY CHECK EARLY: Fail fast with clear error.
     * Don't waste time if inputs are invalid.
     */
    if (buf == NULL || len == 0) {
        LOG_ERR("Invalid TX parameters");
        return -EINVAL;
    }

    /**
     * Step 2: Acquire TX Lock
     *
     * WHY LOCK: Multiple threads might call tx() simultaneously.
     * Without lock, data from different threads could interleave.
     *
     * WHY K_FOREVER: TX is expected to complete. If it doesn't,
     * there's a bug. Using timeout would just hide the bug.
     */
    ret = k_mutex_lock(&data->tx_lock, K_FOREVER);
    if (ret != 0) {
        LOG_ERR("Failed to acquire TX lock: %d", ret);
        return ret;
    }

    /**
     * Step 3: Mode-Specific Transmission
     */
    switch (data->current_mode) {

        case UART_ADV_MODE_POLLING:
            /**
             * Polling Mode - Simple but Blocking
             *
             * HOW IT WORKS:
             * For each byte:
             *   1. Write byte to hardware
             *   2. Wait until hardware has sent it
             *   3. Repeat
             *
             * PROS: Simple, predictable
             * CONS: CPU 100% busy during TX
             */
            for (size_t i = 0; i < len; i++) {
                /* Write byte and wait for completion */
                uart_poll_out(config->uart, buf[i]);
                data->tx_bytes++;
            }
            ret = len;
            break;

        case UART_ADV_MODE_INTERRUPT:
            /**
             * Interrupt Mode - Non-blocking TX
             *
             * HOW IT WORKS:
             * 1. Put all data in ring buffer
             * 2. Enable TX interrupt
             * 3. ISR feeds bytes to hardware
             * 4. Wait for completion semaphore
             *
             * PROS: CPU free while waiting
             * CONS: ISR overhead per byte
             */

            /* Reset completion semaphore */
            k_sem_reset(&data->tx_done);

            /**
             * Put data in ring buffer
             *
             * WHY RING BUFFER: ISR will read from it.
             * Decouples "what to send" from "when to send".
             */
            size_t written = ring_buf_put(&data->tx_ring, buf, len);
            if (written < len) {
                LOG_WRN("TX buffer full, only queued %zu/%zu", written, len);
            }

            /**
             * Enable TX interrupt
             *
             * This starts the transmission process.
             * ISR will handle actual byte-by-byte sending.
             */
            uart_irq_tx_enable(config->uart);

            /**
             * Wait for completion
             *
             * WHY WAIT: Caller expects blocking behavior.
             * Return only when all data sent.
             */
            ret = k_sem_take(&data->tx_done, K_MSEC(1000 + len));
            if (ret == 0) {
                ret = written;  /* Success: return bytes sent */
            } else {
                LOG_ERR("TX timeout");
                uart_irq_tx_disable(config->uart);
                ret = -ETIMEDOUT;
            }
            break;

        case UART_ADV_MODE_ASYNC:
            /**
             * Async/DMA Mode - Hardware does the work
             *
             * HOW IT WORKS:
             * 1. Give DMA the buffer pointer and length
             * 2. DMA reads bytes directly from memory
             * 3. No CPU involvement until complete
             *
             * PROS: Minimal CPU usage, high throughput
             * CONS: More complex setup, buffer lifetime issues
             */
            ret = uart_tx(config->uart, buf, len, SYS_FOREVER_US);
            if (ret == 0) {
                /* Wait for async completion */
                k_sem_take(&data->tx_done, K_FOREVER);
                ret = len;
            }
            break;

        default:
            ret = -ENOTSUP;
    }

    /**
     * Step 4: Release Lock
     *
     * ALWAYS release lock, even on error!
     * Otherwise, no other thread can ever TX.
     */
    k_mutex_unlock(&data->tx_lock);

    return ret;
}
```

### Device Initialization

```c
/**
 * Device Initialization Function
 *
 * CALLED BY: Kernel during boot
 * TIMING: After kernel starts (POST_KERNEL level)
 *
 * GOALS:
 * 1. Validate configuration
 * 2. Initialize kernel objects
 * 3. Configure hardware
 * 4. Leave device in known state
 */
static int uart_advanced_init(const struct device *dev)
{
    const struct uart_advanced_config_hw *config = dev->config;
    struct uart_advanced_data *data = dev->data;
    int ret;

    LOG_INF("Initializing %s", dev->name);

    /**
     * Step 1: Check dependencies
     *
     * WHY: We depend on the underlying UART device.
     * If it's not ready, we can't function.
     *
     * device_is_ready() checks:
     * - Device was found in Device Tree
     * - Device's init function succeeded
     */
    if (!device_is_ready(config->uart)) {
        LOG_ERR("UART device not ready: %s", config->uart->name);
        return -ENODEV;
    }

    /**
     * Step 2: Initialize synchronization primitives
     *
     * WHY HERE: These must be initialized before any use.
     * Can't use them in ISR or other threads until initialized.
     */

    /* Mutex: Starts unlocked (available) */
    k_mutex_init(&data->tx_lock);

    /**
     * Semaphores:
     *   Initial count: 0 (nothing available yet)
     *   Max count: 1 (binary semaphore)
     *
     * Binary semaphore acts as signal:
     *   give() = set to 1 (signal)
     *   take() = wait until 1, then set to 0
     */
    k_sem_init(&data->rx_sem, 0, 1);
    k_sem_init(&data->tx_done, 0, 1);

    /**
     * Step 3: Initialize ring buffers
     *
     * ring_buf_init() associates metadata struct with storage.
     * Size comes from Device Tree configuration.
     */
    ring_buf_init(&data->tx_ring, config->tx_buf_size, data->tx_buf);
    ring_buf_init(&data->rx_ring, config->rx_buf_size, data->rx_buf);

    /**
     * Step 4: Configure UART hardware
     *
     * Apply default settings from Device Tree.
     * User can change later via uart_advanced_configure().
     */
    struct uart_config uart_cfg = {
        .baudrate = config->default_baudrate,
        .parity = config->default_parity,
        .stop_bits = config->default_stop_bits,
        .data_bits = config->default_data_bits,
        .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
    };

    ret = uart_configure(config->uart, &uart_cfg);
    if (ret != 0) {
        LOG_ERR("Failed to configure UART: %d", ret);
        return ret;
    }

    /**
     * Step 5: Setup interrupt callback
     *
     * Register our ISR handler with the UART driver.
     * 'dev' passed as user_data so ISR knows which instance.
     */
    uart_irq_callback_user_data_set(config->uart, uart_isr_handler, (void *)dev);

    /**
     * Enable RX interrupt by default
     *
     * WHY RX NOW: We always want to receive data.
     * TX interrupt enabled only when we have data to send.
     */
    uart_irq_rx_enable(config->uart);

    /* Start with interrupt mode as default */
    data->current_mode = UART_ADV_MODE_INTERRUPT;
    data->enabled = true;

    LOG_INF("Initialization complete");

    return 0;
}
```

### Device Instantiation Macros

```c
/**
 * Buffer Storage Allocation
 *
 * WHY SEPARATE MACRO: Each instance needs its own buffers.
 * Size comes from Device Tree properties.
 *
 * DT_INST_PROP(inst, prop): Gets property value for instance
 * DT_INST_PROP_OR(inst, prop, default): Gets property or default
 */
#define UART_ADV_BUFFERS(inst)                                             \
    static uint8_t uart_adv_tx_buf_##inst[DT_INST_PROP_OR(inst, tx_buffer_size, 256)]; \
    static uint8_t uart_adv_rx_buf_##inst[DT_INST_PROP_OR(inst, rx_buffer_size, 256)];

/**
 * Config Structure Initialization
 *
 * Creates the const config structure with values from Device Tree.
 *
 * Key macros:
 *   DEVICE_DT_GET(node): Get device pointer for a DT node
 *   DT_INST_PHANDLE(inst, prop): Get device tree node from phandle property
 *   DT_INST_PROP(inst, prop): Get property value
 *   DT_INST_PROP_OR(inst, prop, default): Get property or use default
 */
#define UART_ADV_CONFIG(inst)                                               \
    static const struct uart_advanced_config_hw uart_adv_config_##inst = {  \
        /* Get UART device from 'uart' property */                          \
        .uart = DEVICE_DT_GET(DT_INST_PHANDLE(inst, uart)),                \
        /* Default UART settings from DT */                                 \
        .default_baudrate = DT_INST_PROP_OR(inst, current_speed, 115200),  \
        .default_data_bits = DT_INST_PROP_OR(inst, data_bits, 8),          \
        .default_parity = DT_INST_PROP_OR(inst, parity, 0),                \
        .default_stop_bits = DT_INST_PROP_OR(inst, stop_bits, 1),          \
        /* Buffer sizes */                                                  \
        .tx_buf_size = DT_INST_PROP_OR(inst, tx_buffer_size, 256),         \
        .rx_buf_size = DT_INST_PROP_OR(inst, rx_buffer_size, 256),         \
    };

/**
 * Data Structure Initialization
 *
 * Creates the mutable data structure.
 * Sets initial values and links to buffer storage.
 */
#define UART_ADV_DATA(inst)                                                 \
    static struct uart_advanced_data uart_adv_data_##inst = {               \
        .tx_buf = uart_adv_tx_buf_##inst,                                   \
        .rx_buf = uart_adv_rx_buf_##inst,                                   \
        .enabled = false,                                                   \
        .current_mode = UART_ADV_MODE_INTERRUPT,                           \
    };

/**
 * Complete Device Definition
 *
 * DEVICE_DT_INST_DEFINE parameters:
 *   inst:     Instance number (0, 1, 2, ...)
 *   init_fn:  Initialization function
 *   pm:       Power management (NULL if not used)
 *   data:     Pointer to data structure
 *   config:   Pointer to config structure
 *   level:    When to initialize (POST_KERNEL = after kernel starts)
 *   priority: Order within level (90 = after UART driver at 50)
 *   api:      Pointer to API structure (NULL for this driver)
 */
#define UART_ADV_DEFINE(inst)                                               \
    UART_ADV_BUFFERS(inst)                                                  \
    UART_ADV_CONFIG(inst)                                                   \
    UART_ADV_DATA(inst)                                                     \
    DEVICE_DT_INST_DEFINE(inst,                                            \
                          uart_advanced_init,                               \
                          NULL,  /* No PM */                                \
                          &uart_adv_data_##inst,                           \
                          &uart_adv_config_##inst,                         \
                          POST_KERNEL,                                      \
                          CONFIG_UART_ADVANCED_INIT_PRIORITY,              \
                          NULL);  /* No API struct - using direct functions */

/**
 * Instantiate All Devices
 *
 * DT_INST_FOREACH_STATUS_OKAY(fn):
 *   Calls fn(0), fn(1), fn(2), ... for each DT node where:
 *   - compatible = "custom,uart-advanced" (matches DT_DRV_COMPAT)
 *   - status = "okay" (enabled)
 *
 * If your DTS has two nodes with status = "okay", this creates:
 *   - uart_adv_config_0, uart_adv_data_0, device instance 0
 *   - uart_adv_config_1, uart_adv_data_1, device instance 1
 */
DT_INST_FOREACH_STATUS_OKAY(UART_ADV_DEFINE)
```

---

## Device Tree Binding Explained

### File: `dts/bindings/custom,uart-advanced.yaml`

```yaml
# Description shown in documentation
description: |
  Advanced UART driver with multiple operating modes.

  Example usage:
    my_uart: uart-advanced {
        compatible = "custom,uart-advanced";
        uart = <&uart0>;
        current-speed = <115200>;
        tx-buffer-size = <512>;
        rx-buffer-size = <1024>;
    };

# The compatible string - MUST match DT_DRV_COMPAT
compatible: "custom,uart-advanced"

# Required properties - device won't instantiate without these
properties:
  uart:
    type: phandle
    required: true
    description: |
      Reference to underlying UART hardware.

      WHAT IS PHANDLE:
      A "pointer" to another device tree node.
      <&uart0> means "the node labeled uart0"

      WHY:
      Our driver wraps another UART. This tells us which one.

  current-speed:
    type: int
    required: true
    description: |
      Baud rate in bits per second.

      WHY REQUIRED:
      No sensible default - depends on connected device.
      Force user to specify explicitly.

  tx-buffer-size:
    type: int
    default: 256
    description: |
      Transmit ring buffer size in bytes.

      WHY DEFAULT:
      256 is reasonable for most uses.
      User can override for high-throughput applications.

  rx-buffer-size:
    type: int
    default: 256
    description: |
      Receive ring buffer size in bytes.

  data-bits:
    type: int
    default: 8
    enum: [5, 6, 7, 8, 9]
    description: |
      Number of data bits per frame.

      WHY ENUM:
      Only these values are valid. DT compiler will
      reject invalid values at build time.

  parity:
    type: string
    default: "none"
    enum: ["none", "odd", "even"]
    description: |
      Parity bit configuration.

  stop-bits:
    type: int
    default: 1
    enum: [1, 2]
    description: |
      Number of stop bits.
```

---

## How Data Flows

### RX Data Flow (Interrupt Mode)

```
┌─────────────────────────────────────────────────────────────────┐
│                      DATA RECEPTION FLOW                         │
│                                                                  │
│  External Device                                                 │
│       │                                                          │
│       ▼ Serial bits on wire                                      │
│  ┌─────────────┐                                                 │
│  │ UART HARDWARE│ Converts serial to parallel                    │
│  │ RX FIFO     │                                                 │
│  └──────┬──────┘                                                 │
│         │ RX interrupt (FIFO not empty)                          │
│         ▼                                                        │
│  ┌─────────────┐                                                 │
│  │     ISR     │ uart_isr_handler()                             │
│  │             │                                                 │
│  │  1. Read from UART FIFO                                      │
│  │  2. Put in ring buffer                                       │
│  │  3. k_sem_give(&rx_sem)                                      │
│  │  4. Call user callback                                       │
│  └──────┬──────┘                                                 │
│         │                                                        │
│         ▼                                                        │
│  ┌─────────────┐                                                 │
│  │ RING BUFFER │ Decouples ISR from thread                      │
│  │ [████░░░░░] │                                                 │
│  └──────┬──────┘                                                 │
│         │ Thread wakes up (sem given)                            │
│         ▼                                                        │
│  ┌─────────────┐                                                 │
│  │   THREAD    │ uart_advanced_rx()                             │
│  │             │                                                 │
│  │  1. k_sem_take(&rx_sem, timeout)                             │
│  │  2. Read from ring buffer                                    │
│  │  3. Return data to application                               │
│  └─────────────┘                                                 │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### TX Data Flow (Interrupt Mode)

```
┌─────────────────────────────────────────────────────────────────┐
│                     DATA TRANSMISSION FLOW                       │
│                                                                  │
│  APPLICATION                                                     │
│       │ uart_advanced_tx(dev, data, len)                        │
│       ▼                                                         │
│  ┌─────────────┐                                                 │
│  │   THREAD    │                                                 │
│  │             │                                                 │
│  │  1. k_mutex_lock(&tx_lock)                                   │
│  │  2. Put data in ring buffer                                  │
│  │  3. uart_irq_tx_enable()                                     │
│  │  4. k_sem_take(&tx_done)  ← Waits here                       │
│  │  5. k_mutex_unlock(&tx_lock)                                 │
│  └──────┬──────┘                                                 │
│         │                                                        │
│         ▼                                                        │
│  ┌─────────────┐                                                 │
│  │ RING BUFFER │                                                 │
│  │ [████████░░]│ Contains data to send                          │
│  └──────┬──────┘                                                 │
│         │ TX interrupt enabled                                   │
│         ▼                                                        │
│  ┌─────────────┐                                                 │
│  │     ISR     │ (TX ready interrupt)                           │
│  │             │                                                 │
│  │  Loop:                                                       │
│  │  1. Get byte from ring buffer                                │
│  │  2. Write to UART FIFO                                       │
│  │  3. If buffer empty: disable TX IRQ, give(tx_done)           │
│  └──────┬──────┘                                                 │
│         │                                                        │
│         ▼                                                        │
│  ┌─────────────┐                                                 │
│  │ UART HARDWARE│                                                │
│  │ TX FIFO     │ Converts parallel to serial                    │
│  └──────┬──────┘                                                 │
│         │                                                        │
│         ▼                                                        │
│  External Device (bits on wire)                                  │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## Key Design Decisions

### Decision 1: Ring Buffer vs. Linear Buffer

**Why Ring Buffer:**

```
LINEAR BUFFER (what we DON'T use):
[DATA][DATA][DATA][░░░░░░░░░░░░]
                   ↑
                   New data goes here

After reading some data:
[░░░░][DATA][DATA][░░░░░░░░░░░░]
 ↑
 Wasted space!

Must "compact" by moving data:
[DATA][DATA][░░░░░░░░░░░░░░░░░░]
 - Requires memmove()
 - Not ISR-safe
 - O(n) complexity

RING BUFFER (what we use):
Position: 0  1  2  3  4  5  6  7  8  9
         [░][░][D][D][D][D][D][░][░][░]
              ↑              ↑
              Head (read)    Tail (write)

After reading:
         [░][░][░][░][░][D][D][░][░][░]
                        ↑     ↑
                        Head  Tail

After writing (wraps around!):
         [D][D][░][░][░][D][D][░][░][░]
              ↑        ↑
              Tail     Head

Benefits:
- No data movement needed
- O(1) read and write
- ISR-safe (single producer, single consumer)
- Efficient memory usage
```

### Decision 2: Semaphore vs. Polling for RX

**Why Semaphore:**

```c
/* POLLING (what we DON'T do) */
int poll_rx(uint8_t *buf, size_t len)
{
    while (ring_buf_is_empty(&rx_ring)) {
        /* Busy waiting - 100% CPU usage! */
    }
    return ring_buf_get(&rx_ring, buf, len);
}

/* SEMAPHORE (what we DO) */
int sem_rx(uint8_t *buf, size_t len, k_timeout_t timeout)
{
    /* Blocks efficiently - CPU does other work */
    int ret = k_sem_take(&rx_sem, timeout);
    if (ret != 0) {
        return -ETIMEDOUT;
    }
    return ring_buf_get(&rx_ring, buf, len);
}

/*
 * CPU usage comparison:
 *
 * Polling: ████████████████████████  100% busy
 * Semaphore: ░░░░▓░░░░░░░▓░░░░░░░▓  ~5% (only when data arrives)
 */
```

### Decision 3: Mutex for TX, Not RX

**Why different synchronization:**

```
TX SCENARIO:
- Multiple threads might call tx() simultaneously
- Order matters (don't interleave messages)
- Need mutual exclusion → MUTEX

Thread A: tx("Hello ")
Thread B: tx("World")

Without mutex: "HWeolrllod "  ← Garbled!
With mutex:    "Hello World"  ← Correct!

RX SCENARIO:
- Usually one thread reads
- ISR produces, thread consumes
- Need signaling, not exclusion → SEMAPHORE

ISR:    put_data() → k_sem_give()
Thread: k_sem_take() → get_data()
```

---

## Summary

The UART driver demonstrates:

1. **Layered architecture**: Public API → Implementation → Hardware access
2. **Multiple operating modes**: Same interface, different behavior
3. **Thread safety**: Mutex for TX, semaphore for RX
4. **ISR design**: Minimal work, defer to thread
5. **Ring buffers**: Efficient ISR-to-thread communication
6. **Device Tree integration**: All configuration external
7. **Multi-instance support**: No globals, per-instance data
