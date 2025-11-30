/**
 * @file uart_advanced.c
 * @brief Advanced UART Driver Implementation
 *
 * This driver wraps Zephyr's UART API to provide additional features:
 * - Ring buffer management for interrupt mode
 * - Unified callback interface across modes
 * - Statistics tracking
 * - Convenience functions (printf, readline)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT custom_uart_advanced

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/logging/log.h>
#include <stdarg.h>
#include <stdio.h>

#include "uart_advanced.h"

LOG_MODULE_REGISTER(uart_advanced, CONFIG_UART_ADVANCED_LOG_LEVEL);

/* Ring buffer sizes from Kconfig */
#define RX_BUF_SIZE CONFIG_UART_ADVANCED_RX_BUFFER_SIZE
#define TX_BUF_SIZE CONFIG_UART_ADVANCED_TX_BUFFER_SIZE

/**
 * @brief Driver runtime data
 */
struct uart_adv_data {
    /* Underlying UART device */
    const struct device *uart_dev;

    /* Operating mode */
    enum uart_adv_mode mode;

    /* Current configuration */
    struct uart_adv_config config;

    /* Callback */
    uart_adv_callback_t callback;
    void *user_data;

    /* Ring buffers for interrupt mode */
    struct ring_buf rx_ring;
    struct ring_buf tx_ring;
    uint8_t rx_buffer[RX_BUF_SIZE];
    uint8_t tx_buffer[TX_BUF_SIZE];

    /* Synchronization */
    struct k_sem tx_sem;
    struct k_sem rx_sem;
    struct k_mutex lock;

    /* Async mode state */
#ifdef CONFIG_UART_ADVANCED_ASYNC
    uint8_t *async_rx_buf;
    size_t async_rx_len;
    volatile bool async_tx_pending;
    volatile bool async_rx_enabled;
#endif

    /* Statistics */
    struct uart_adv_stats stats;

    /* Flags */
    bool initialized;
    bool irq_rx_enabled;
    bool irq_tx_enabled;
};

/**
 * @brief Driver configuration (from device tree)
 */
struct uart_adv_cfg {
    const struct device *uart_dev;
    uint32_t default_baudrate;
};

/* ========== Internal Ring Buffer Helpers ========== */

static inline bool rx_ring_is_empty(struct uart_adv_data *data)
{
    return ring_buf_is_empty(&data->rx_ring);
}

static inline bool tx_ring_is_empty(struct uart_adv_data *data)
{
    return ring_buf_is_empty(&data->tx_ring);
}

static inline size_t rx_ring_space(struct uart_adv_data *data)
{
    return ring_buf_space_get(&data->rx_ring);
}

static inline size_t tx_ring_space(struct uart_adv_data *data)
{
    return ring_buf_space_get(&data->tx_ring);
}

/* ========== UART Callback Handlers ========== */

static void uart_irq_handler(const struct device *uart_dev, void *user_data)
{
    const struct device *dev = user_data;
    struct uart_adv_data *data = dev->data;
    uint8_t byte;
    int ret;

    while (uart_irq_update(uart_dev) && uart_irq_is_pending(uart_dev)) {
        /* Handle RX */
        if (uart_irq_rx_ready(uart_dev)) {
            while (uart_fifo_read(uart_dev, &byte, 1) == 1) {
                ret = ring_buf_put(&data->rx_ring, &byte, 1);
                if (ret == 0) {
                    data->stats.overrun_errors++;
                    LOG_WRN("RX buffer overrun");
                } else {
                    data->stats.rx_bytes++;
                }
            }
            k_sem_give(&data->rx_sem);

            /* Notify via callback */
            if (data->callback) {
                struct uart_adv_event evt = {
                    .type = UART_ADV_EVT_RX_RDY,
                    .data.rx.len = ring_buf_size_get(&data->rx_ring),
                };
                data->callback(dev, &evt, data->user_data);
            }
        }

        /* Handle TX */
        if (uart_irq_tx_ready(uart_dev)) {
            if (!ring_buf_is_empty(&data->tx_ring)) {
                ret = ring_buf_get(&data->tx_ring, &byte, 1);
                if (ret == 1) {
                    uart_fifo_fill(uart_dev, &byte, 1);
                    data->stats.tx_bytes++;
                }
            } else {
                /* TX buffer empty, disable TX interrupt */
                uart_irq_tx_disable(uart_dev);
                data->irq_tx_enabled = false;
                k_sem_give(&data->tx_sem);

                /* Notify via callback */
                if (data->callback) {
                    struct uart_adv_event evt = {
                        .type = UART_ADV_EVT_TX_DONE,
                    };
                    data->callback(dev, &evt, data->user_data);
                }
            }
        }
    }
}

#ifdef CONFIG_UART_ADVANCED_ASYNC
static void uart_async_callback(const struct device *uart_dev,
                                struct uart_event *evt,
                                void *user_data)
{
    const struct device *dev = user_data;
    struct uart_adv_data *data = dev->data;

    switch (evt->type) {
    case UART_TX_DONE:
        data->async_tx_pending = false;
        data->stats.tx_bytes += evt->data.tx.len;
        if (data->callback) {
            struct uart_adv_event adv_evt = {
                .type = UART_ADV_EVT_TX_DONE,
                .data.tx.buf = (uint8_t *)evt->data.tx.buf,
                .data.tx.len = evt->data.tx.len,
            };
            data->callback(dev, &adv_evt, data->user_data);
        }
        break;

    case UART_TX_ABORTED:
        data->async_tx_pending = false;
        if (data->callback) {
            struct uart_adv_event adv_evt = {
                .type = UART_ADV_EVT_TX_ABORTED,
            };
            data->callback(dev, &adv_evt, data->user_data);
        }
        break;

    case UART_RX_RDY:
        data->stats.rx_bytes += evt->data.rx.len;
        if (data->callback) {
            struct uart_adv_event adv_evt = {
                .type = UART_ADV_EVT_RX_RDY,
                .data.rx.buf = evt->data.rx.buf + evt->data.rx.offset,
                .data.rx.len = evt->data.rx.len,
            };
            data->callback(dev, &adv_evt, data->user_data);
        }
        break;

    case UART_RX_BUF_REQUEST:
        if (data->callback) {
            struct uart_adv_event adv_evt = {
                .type = UART_ADV_EVT_RX_BUF_REQ,
            };
            data->callback(dev, &adv_evt, data->user_data);
        }
        break;

    case UART_RX_BUF_RELEASED:
        if (data->callback) {
            struct uart_adv_event adv_evt = {
                .type = UART_ADV_EVT_RX_BUF_REL,
                .data.rx.buf = evt->data.rx_buf.buf,
            };
            data->callback(dev, &adv_evt, data->user_data);
        }
        break;

    case UART_RX_STOPPED:
        data->async_rx_enabled = false;
        if (data->callback) {
            struct uart_adv_event adv_evt = {
                .type = UART_ADV_EVT_RX_STOPPED,
            };
            data->callback(dev, &adv_evt, data->user_data);
        }
        break;

    case UART_RX_DISABLED:
        data->async_rx_enabled = false;
        break;

    default:
        break;
    }
}
#endif /* CONFIG_UART_ADVANCED_ASYNC */

/* ========== Public API Implementation ========== */

int uart_adv_configure(const struct device *dev,
                       const struct uart_adv_config *cfg)
{
    struct uart_adv_data *data = dev->data;
    struct uart_config uart_cfg;
    int ret;

    if (cfg == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&data->lock, K_FOREVER);

    /* Convert to Zephyr UART config */
    uart_cfg.baudrate = cfg->baudrate;
    uart_cfg.data_bits = cfg->data_bits;

    switch (cfg->parity) {
    case UART_ADV_PARITY_NONE:
        uart_cfg.parity = UART_CFG_PARITY_NONE;
        break;
    case UART_ADV_PARITY_ODD:
        uart_cfg.parity = UART_CFG_PARITY_ODD;
        break;
    case UART_ADV_PARITY_EVEN:
        uart_cfg.parity = UART_CFG_PARITY_EVEN;
        break;
    default:
        k_mutex_unlock(&data->lock);
        return -EINVAL;
    }

    switch (cfg->stop_bits) {
    case UART_ADV_STOPBITS_1:
        uart_cfg.stop_bits = UART_CFG_STOP_BITS_1;
        break;
    case UART_ADV_STOPBITS_2:
        uart_cfg.stop_bits = UART_CFG_STOP_BITS_2;
        break;
    default:
        k_mutex_unlock(&data->lock);
        return -EINVAL;
    }

    switch (cfg->flow_ctrl) {
    case UART_ADV_FLOW_CTRL_NONE:
        uart_cfg.flow_ctrl = UART_CFG_FLOW_CTRL_NONE;
        break;
    case UART_ADV_FLOW_CTRL_RTS_CTS:
        uart_cfg.flow_ctrl = UART_CFG_FLOW_CTRL_RTS_CTS;
        break;
    default:
        k_mutex_unlock(&data->lock);
        return -EINVAL;
    }

    ret = uart_configure(data->uart_dev, &uart_cfg);
    if (ret == 0) {
        data->config = *cfg;
    }

    k_mutex_unlock(&data->lock);

    LOG_DBG("Configured: %u baud, %u data bits",
            cfg->baudrate, cfg->data_bits);

    return ret;
}

int uart_adv_config_get(const struct device *dev,
                        struct uart_adv_config *cfg)
{
    struct uart_adv_data *data = dev->data;

    if (cfg == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&data->lock, K_FOREVER);
    *cfg = data->config;
    k_mutex_unlock(&data->lock);

    return 0;
}

int uart_adv_mode_set(const struct device *dev, enum uart_adv_mode mode)
{
    struct uart_adv_data *data = dev->data;

    k_mutex_lock(&data->lock, K_FOREVER);

    /* Disable current mode */
    switch (data->mode) {
    case UART_ADV_MODE_INT:
        uart_irq_rx_disable(data->uart_dev);
        uart_irq_tx_disable(data->uart_dev);
        data->irq_rx_enabled = false;
        data->irq_tx_enabled = false;
        break;
#ifdef CONFIG_UART_ADVANCED_ASYNC
    case UART_ADV_MODE_ASYNC:
        if (data->async_rx_enabled) {
            uart_rx_disable(data->uart_dev);
            data->async_rx_enabled = false;
        }
        break;
#endif
    default:
        break;
    }

    data->mode = mode;

    /* Enable new mode */
    switch (mode) {
    case UART_ADV_MODE_INT:
        uart_irq_callback_user_data_set(data->uart_dev,
                                        uart_irq_handler,
                                        (void *)dev);
        break;
#ifdef CONFIG_UART_ADVANCED_ASYNC
    case UART_ADV_MODE_ASYNC:
        uart_callback_set(data->uart_dev,
                          uart_async_callback,
                          (void *)dev);
        break;
#endif
    default:
        break;
    }

    k_mutex_unlock(&data->lock);

    LOG_DBG("Mode set to %d", mode);

    return 0;
}

int uart_adv_callback_set(const struct device *dev,
                          uart_adv_callback_t callback,
                          void *user_data)
{
    struct uart_adv_data *data = dev->data;

    k_mutex_lock(&data->lock, K_FOREVER);
    data->callback = callback;
    data->user_data = user_data;
    k_mutex_unlock(&data->lock);

    return 0;
}

/* ========== Polling Mode ========== */

int uart_adv_poll_out(const struct device *dev, uint8_t byte)
{
    struct uart_adv_data *data = dev->data;

    uart_poll_out(data->uart_dev, byte);
    data->stats.tx_bytes++;

    return 0;
}

int uart_adv_poll_in(const struct device *dev, uint8_t *byte,
                     k_timeout_t timeout)
{
    struct uart_adv_data *data = dev->data;
    int ret;
    int64_t end_time = 0;

    if (byte == NULL) {
        return -EINVAL;
    }

    if (!K_TIMEOUT_EQ(timeout, K_NO_WAIT) &&
        !K_TIMEOUT_EQ(timeout, K_FOREVER)) {
        end_time = k_uptime_get() + k_ticks_to_ms_floor64(timeout.ticks);
    }

    while (1) {
        ret = uart_poll_in(data->uart_dev, byte);
        if (ret == 0) {
            data->stats.rx_bytes++;
            return 0;
        }

        if (K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
            return -EAGAIN;
        }

        if (!K_TIMEOUT_EQ(timeout, K_FOREVER) &&
            k_uptime_get() >= end_time) {
            return -ETIMEDOUT;
        }

        k_yield();
    }
}

int uart_adv_poll_write(const struct device *dev,
                        const uint8_t *buf, size_t len)
{
    struct uart_adv_data *data = dev->data;

    if (buf == NULL || len == 0) {
        return -EINVAL;
    }

    for (size_t i = 0; i < len; i++) {
        uart_poll_out(data->uart_dev, buf[i]);
    }

    data->stats.tx_bytes += len;

    return len;
}

int uart_adv_poll_read(const struct device *dev,
                       uint8_t *buf, size_t len,
                       k_timeout_t timeout)
{
    struct uart_adv_data *data = dev->data;
    size_t count = 0;
    int ret;
    int64_t end_time = 0;

    if (buf == NULL || len == 0) {
        return -EINVAL;
    }

    if (!K_TIMEOUT_EQ(timeout, K_NO_WAIT) &&
        !K_TIMEOUT_EQ(timeout, K_FOREVER)) {
        end_time = k_uptime_get() + k_ticks_to_ms_floor64(timeout.ticks);
    }

    while (count < len) {
        ret = uart_poll_in(data->uart_dev, &buf[count]);
        if (ret == 0) {
            count++;
            data->stats.rx_bytes++;
            continue;
        }

        if (count > 0) {
            /* Return what we have if we've received something */
            return count;
        }

        if (K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
            return -EAGAIN;
        }

        if (!K_TIMEOUT_EQ(timeout, K_FOREVER) &&
            k_uptime_get() >= end_time) {
            return count > 0 ? count : -ETIMEDOUT;
        }

        k_yield();
    }

    return count;
}

/* ========== Interrupt Mode ========== */

int uart_adv_irq_rx_enable(const struct device *dev)
{
    struct uart_adv_data *data = dev->data;

    if (data->mode != UART_ADV_MODE_INT) {
        return -EINVAL;
    }

    k_mutex_lock(&data->lock, K_FOREVER);
    uart_irq_rx_enable(data->uart_dev);
    data->irq_rx_enabled = true;
    k_mutex_unlock(&data->lock);

    LOG_DBG("IRQ RX enabled");

    return 0;
}

int uart_adv_irq_rx_disable(const struct device *dev)
{
    struct uart_adv_data *data = dev->data;

    k_mutex_lock(&data->lock, K_FOREVER);
    uart_irq_rx_disable(data->uart_dev);
    data->irq_rx_enabled = false;
    k_mutex_unlock(&data->lock);

    return 0;
}

int uart_adv_irq_write(const struct device *dev,
                       const uint8_t *buf, size_t len)
{
    struct uart_adv_data *data = dev->data;
    size_t written;

    if (buf == NULL || len == 0) {
        return -EINVAL;
    }

    if (data->mode != UART_ADV_MODE_INT) {
        return -EINVAL;
    }

    k_mutex_lock(&data->lock, K_FOREVER);

    written = ring_buf_put(&data->tx_ring, buf, len);

    if (!data->irq_tx_enabled && written > 0) {
        uart_irq_tx_enable(data->uart_dev);
        data->irq_tx_enabled = true;
    }

    k_mutex_unlock(&data->lock);

    if (written < len) {
        LOG_WRN("TX buffer full, %zu of %zu bytes queued",
                written, len);
    }

    return written;
}

int uart_adv_irq_read(const struct device *dev,
                      uint8_t *buf, size_t len)
{
    struct uart_adv_data *data = dev->data;
    size_t read;

    if (buf == NULL || len == 0) {
        return -EINVAL;
    }

    k_mutex_lock(&data->lock, K_FOREVER);
    read = ring_buf_get(&data->rx_ring, buf, len);
    k_mutex_unlock(&data->lock);

    return read;
}

size_t uart_adv_irq_rx_available(const struct device *dev)
{
    struct uart_adv_data *data = dev->data;

    return ring_buf_size_get(&data->rx_ring);
}

/* ========== Async/DMA Mode ========== */

#ifdef CONFIG_UART_ADVANCED_ASYNC
int uart_adv_async_tx(const struct device *dev,
                      const uint8_t *buf, size_t len,
                      k_timeout_t timeout)
{
    struct uart_adv_data *data = dev->data;
    int ret;

    if (buf == NULL || len == 0) {
        return -EINVAL;
    }

    if (data->mode != UART_ADV_MODE_ASYNC) {
        return -EINVAL;
    }

    k_mutex_lock(&data->lock, K_FOREVER);

    if (data->async_tx_pending) {
        k_mutex_unlock(&data->lock);
        return -EBUSY;
    }

    data->async_tx_pending = true;
    ret = uart_tx(data->uart_dev, buf, len, SYS_FOREVER_US);

    if (ret != 0) {
        data->async_tx_pending = false;
    }

    k_mutex_unlock(&data->lock);

    return ret;
}

int uart_adv_async_tx_abort(const struct device *dev)
{
    struct uart_adv_data *data = dev->data;

    return uart_tx_abort(data->uart_dev);
}

int uart_adv_async_rx_enable(const struct device *dev,
                             uint8_t *buf, size_t len,
                             k_timeout_t timeout)
{
    struct uart_adv_data *data = dev->data;
    int ret;

    if (buf == NULL || len == 0) {
        return -EINVAL;
    }

    if (data->mode != UART_ADV_MODE_ASYNC) {
        return -EINVAL;
    }

    k_mutex_lock(&data->lock, K_FOREVER);

    data->async_rx_buf = buf;
    data->async_rx_len = len;

    int32_t timeout_us = K_TIMEOUT_EQ(timeout, K_FOREVER) ?
                         SYS_FOREVER_US :
                         k_ticks_to_us_floor32(timeout.ticks);

    ret = uart_rx_enable(data->uart_dev, buf, len, timeout_us);

    if (ret == 0) {
        data->async_rx_enabled = true;
    }

    k_mutex_unlock(&data->lock);

    return ret;
}

int uart_adv_async_rx_buf_rsp(const struct device *dev,
                              uint8_t *buf, size_t len)
{
    struct uart_adv_data *data = dev->data;

    if (buf == NULL || len == 0) {
        return -EINVAL;
    }

    return uart_rx_buf_rsp(data->uart_dev, buf, len);
}

int uart_adv_async_rx_disable(const struct device *dev)
{
    struct uart_adv_data *data = dev->data;

    return uart_rx_disable(data->uart_dev);
}
#endif /* CONFIG_UART_ADVANCED_ASYNC */

/* ========== Utility Functions ========== */

int uart_adv_tx_flush(const struct device *dev)
{
    struct uart_adv_data *data = dev->data;

    k_mutex_lock(&data->lock, K_FOREVER);
    ring_buf_reset(&data->tx_ring);
    k_mutex_unlock(&data->lock);

    return 0;
}

int uart_adv_rx_flush(const struct device *dev)
{
    struct uart_adv_data *data = dev->data;

    k_mutex_lock(&data->lock, K_FOREVER);
    ring_buf_reset(&data->rx_ring);
    k_mutex_unlock(&data->lock);

    return 0;
}

int uart_adv_stats_get(const struct device *dev,
                       struct uart_adv_stats *stats)
{
    struct uart_adv_data *data = dev->data;

    if (stats == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&data->lock, K_FOREVER);
    *stats = data->stats;
    k_mutex_unlock(&data->lock);

    return 0;
}

int uart_adv_stats_reset(const struct device *dev)
{
    struct uart_adv_data *data = dev->data;

    k_mutex_lock(&data->lock, K_FOREVER);
    memset(&data->stats, 0, sizeof(data->stats));
    k_mutex_unlock(&data->lock);

    return 0;
}

int uart_adv_printf(const struct device *dev, const char *fmt, ...)
{
    char buf[128];
    va_list args;
    int len;

    va_start(args, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len < 0) {
        return len;
    }

    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1;
    }

    return uart_adv_poll_write(dev, (uint8_t *)buf, len);
}

int uart_adv_readline(const struct device *dev,
                      char *buf, size_t len,
                      k_timeout_t timeout)
{
    struct uart_adv_data *data = dev->data;
    size_t count = 0;
    uint8_t c;
    int ret;
    int64_t end_time = 0;

    if (buf == NULL || len == 0) {
        return -EINVAL;
    }

    if (!K_TIMEOUT_EQ(timeout, K_NO_WAIT) &&
        !K_TIMEOUT_EQ(timeout, K_FOREVER)) {
        end_time = k_uptime_get() + k_ticks_to_ms_floor64(timeout.ticks);
    }

    while (count < len - 1) {
        ret = uart_adv_poll_in(dev, &c, K_MSEC(10));

        if (ret == 0) {
            if (c == '\n' || c == '\r') {
                break;
            }
            buf[count++] = c;
            continue;
        }

        if (K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
            break;
        }

        if (!K_TIMEOUT_EQ(timeout, K_FOREVER) &&
            k_uptime_get() >= end_time) {
            break;
        }
    }

    buf[count] = '\0';
    return count;
}

/* ========== Initialization ========== */

static int uart_adv_init(const struct device *dev)
{
    const struct uart_adv_cfg *config = dev->config;
    struct uart_adv_data *data = dev->data;
    int ret;

    LOG_DBG("Initializing %s", dev->name);

    /* Check underlying UART device */
    if (!device_is_ready(config->uart_dev)) {
        LOG_ERR("UART device not ready");
        return -ENODEV;
    }

    data->uart_dev = config->uart_dev;

    /* Initialize ring buffers */
    ring_buf_init(&data->rx_ring, sizeof(data->rx_buffer), data->rx_buffer);
    ring_buf_init(&data->tx_ring, sizeof(data->tx_buffer), data->tx_buffer);

    /* Initialize synchronization */
    k_sem_init(&data->tx_sem, 0, 1);
    k_sem_init(&data->rx_sem, 0, 1);
    k_mutex_init(&data->lock);

    /* Set default configuration */
    data->config.baudrate = config->default_baudrate;
    data->config.parity = UART_ADV_PARITY_NONE;
    data->config.stop_bits = UART_ADV_STOPBITS_1;
    data->config.data_bits = UART_ADV_DATABITS_8;
    data->config.flow_ctrl = UART_ADV_FLOW_CTRL_NONE;

    /* Default to polling mode */
    data->mode = UART_ADV_MODE_POLL;

    data->initialized = true;

    LOG_INF("Initialized (UART: %s, baud: %u)",
            config->uart_dev->name, config->default_baudrate);

    return 0;
}

/* ========== Device Instantiation ========== */

#define UART_ADV_INIT(inst)                                                 \
    static struct uart_adv_data uart_adv_data_##inst;                       \
                                                                            \
    static const struct uart_adv_cfg uart_adv_cfg_##inst = {                \
        .uart_dev = DEVICE_DT_GET(DT_INST_PHANDLE(inst, uart_device)),     \
        .default_baudrate = DT_INST_PROP_OR(inst, current_speed, 115200),  \
    };                                                                      \
                                                                            \
    DEVICE_DT_INST_DEFINE(inst,                                            \
                          uart_adv_init,                                    \
                          NULL,                                             \
                          &uart_adv_data_##inst,                            \
                          &uart_adv_cfg_##inst,                             \
                          POST_KERNEL,                                      \
                          CONFIG_UART_ADVANCED_INIT_PRIORITY,               \
                          NULL);

DT_INST_FOREACH_STATUS_OKAY(UART_ADV_INIT)
