/**
 * @file spi_advanced.c
 * @brief Advanced SPI Driver Implementation
 *
 * This driver provides extended SPI functionality including:
 * - Multi-device management with individual configurations
 * - DMA transfer support
 * - Async operations with callbacks
 * - Comprehensive statistics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT custom_spi_advanced

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/timing/timing.h>
#include <string.h>

#include "spi_advanced.h"

LOG_MODULE_REGISTER(spi_advanced, CONFIG_SPI_ADVANCED_LOG_LEVEL);

/* Maximum number of SPI devices per bus */
#define SPI_ADV_MAX_DEVICES 8

/* Buffer size for internal operations */
#define BUFFER_SIZE CONFIG_SPI_ADVANCED_BUFFER_SIZE

/**
 * @brief Per-device runtime data
 */
struct spi_adv_device_data {
    bool configured;
    struct spi_adv_device_config config;
    struct spi_config spi_cfg;
    struct spi_cs_control cs_ctrl;
};

/**
 * @brief Driver runtime data
 */
struct spi_adv_data {
    /* Underlying SPI device */
    const struct device *spi_dev;

    /* Per-device configuration */
    struct spi_adv_device_data devices[SPI_ADV_MAX_DEVICES];

    /* Synchronization */
    struct k_mutex lock;
    struct k_sem async_sem;

    /* Async state */
#ifdef CONFIG_SPI_ADVANCED_ASYNC
    spi_adv_callback_t async_callback;
    void *async_user_data;
    volatile bool async_pending;
#endif

    /* Statistics */
#ifdef CONFIG_SPI_ADVANCED_STATS
    struct spi_adv_stats stats;
#endif

    /* Internal buffer */
    uint8_t buffer[BUFFER_SIZE];

    bool initialized;
};

/**
 * @brief Driver configuration
 */
struct spi_adv_cfg {
    const struct device *spi_dev;
    uint32_t max_frequency;
};

/* ========== Internal Functions ========== */

static inline void update_timing_stats(struct spi_adv_data *data,
                                       uint32_t duration_us)
{
#ifdef CONFIG_SPI_ADVANCED_STATS
    data->stats.last_transfer_us = duration_us;
    if (duration_us > data->stats.max_transfer_us) {
        data->stats.max_transfer_us = duration_us;
    }
#endif
}

static int build_spi_config(struct spi_adv_device_data *dev_data)
{
    struct spi_adv_device_config *cfg = &dev_data->config;
    struct spi_config *spi_cfg = &dev_data->spi_cfg;

    spi_cfg->frequency = cfg->frequency;
    spi_cfg->operation = SPI_WORD_SET(cfg->word_size);

    /* Set SPI mode */
    switch (cfg->mode) {
    case SPI_ADV_MODE_0:
        /* CPOL=0, CPHA=0 - default */
        break;
    case SPI_ADV_MODE_1:
        spi_cfg->operation |= SPI_MODE_CPHA;
        break;
    case SPI_ADV_MODE_2:
        spi_cfg->operation |= SPI_MODE_CPOL;
        break;
    case SPI_ADV_MODE_3:
        spi_cfg->operation |= SPI_MODE_CPOL | SPI_MODE_CPHA;
        break;
    }

    /* Set bit order */
    if (cfg->bit_order == SPI_ADV_LSB_FIRST) {
        spi_cfg->operation |= SPI_TRANSFER_LSB;
    }

    /* Configure chip select if available */
    if (cfg->cs_gpio.port != NULL) {
        dev_data->cs_ctrl.gpio = cfg->cs_gpio;
        dev_data->cs_ctrl.delay = cfg->cs_delay_us;
        spi_cfg->cs = dev_data->cs_ctrl;
    } else {
        spi_cfg->cs.gpio.port = NULL;
    }

    return 0;
}

static int do_spi_transfer(const struct device *dev, uint8_t device_id,
                           const struct spi_buf_set *tx_bufs,
                           const struct spi_buf_set *rx_bufs)
{
    struct spi_adv_data *data = dev->data;
    struct spi_adv_device_data *dev_data = &data->devices[device_id];
    int ret;

#ifdef CONFIG_TIMING_FUNCTIONS
    timing_t start = timing_counter_get();
#endif

    ret = spi_transceive(data->spi_dev, &dev_data->spi_cfg, tx_bufs, rx_bufs);

#ifdef CONFIG_TIMING_FUNCTIONS
    timing_t end = timing_counter_get();
    uint64_t cycles = timing_cycles_get(&start, &end);
    uint32_t us = (uint32_t)timing_cycles_to_ns(cycles) / 1000;
    update_timing_stats(data, us);
#endif

#ifdef CONFIG_SPI_ADVANCED_STATS
    if (ret == 0) {
        data->stats.transfers++;
        if (tx_bufs != NULL) {
            for (size_t i = 0; i < tx_bufs->count; i++) {
                data->stats.tx_bytes += tx_bufs->buffers[i].len;
            }
        }
        if (rx_bufs != NULL) {
            for (size_t i = 0; i < rx_bufs->count; i++) {
                data->stats.rx_bytes += rx_bufs->buffers[i].len;
            }
        }
    } else {
        data->stats.errors++;
    }
#endif

    return ret;
}

/* ========== Public API Implementation ========== */

int spi_adv_device_configure(const struct device *dev,
                             uint8_t device_id,
                             const struct spi_adv_device_config *cfg)
{
    struct spi_adv_data *data = dev->data;
    struct spi_adv_device_data *dev_data;
    int ret;

    if (cfg == NULL || device_id >= SPI_ADV_MAX_DEVICES) {
        return -EINVAL;
    }

    k_mutex_lock(&data->lock, K_FOREVER);

    dev_data = &data->devices[device_id];
    dev_data->config = *cfg;

    /* Configure GPIO for chip select */
    if (cfg->cs_gpio.port != NULL) {
        if (!gpio_is_ready_dt(&cfg->cs_gpio)) {
            LOG_ERR("CS GPIO not ready");
            k_mutex_unlock(&data->lock);
            return -ENODEV;
        }

        ret = gpio_pin_configure_dt(&cfg->cs_gpio,
            (cfg->cs_ctrl == SPI_ADV_CS_ACTIVE_LOW) ?
            GPIO_OUTPUT_HIGH : GPIO_OUTPUT_LOW);
        if (ret < 0) {
            LOG_ERR("Failed to configure CS GPIO: %d", ret);
            k_mutex_unlock(&data->lock);
            return ret;
        }
    }

    /* Build Zephyr SPI config */
    ret = build_spi_config(dev_data);
    if (ret < 0) {
        k_mutex_unlock(&data->lock);
        return ret;
    }

    dev_data->configured = true;

    k_mutex_unlock(&data->lock);

    LOG_INF("Device %u configured: %u Hz, mode %d, %u bits",
            device_id, cfg->frequency, cfg->mode, cfg->word_size);

    return 0;
}

int spi_adv_device_config_get(const struct device *dev,
                              uint8_t device_id,
                              struct spi_adv_device_config *cfg)
{
    struct spi_adv_data *data = dev->data;

    if (cfg == NULL || device_id >= SPI_ADV_MAX_DEVICES) {
        return -EINVAL;
    }

    if (!data->devices[device_id].configured) {
        return -ENOENT;
    }

    k_mutex_lock(&data->lock, K_FOREVER);
    *cfg = data->devices[device_id].config;
    k_mutex_unlock(&data->lock);

    return 0;
}

int spi_adv_write(const struct device *dev, uint8_t device_id,
                  const uint8_t *buf, size_t len)
{
    struct spi_adv_data *data = dev->data;
    int ret;

    if (buf == NULL || len == 0 || device_id >= SPI_ADV_MAX_DEVICES) {
        return -EINVAL;
    }

    if (!data->devices[device_id].configured) {
        return -ENOENT;
    }

    struct spi_buf tx_buf = {
        .buf = (void *)buf,
        .len = len,
    };
    struct spi_buf_set tx_bufs = {
        .buffers = &tx_buf,
        .count = 1,
    };

    k_mutex_lock(&data->lock, K_FOREVER);
    ret = do_spi_transfer(dev, device_id, &tx_bufs, NULL);
    k_mutex_unlock(&data->lock);

    return ret == 0 ? len : ret;
}

int spi_adv_read(const struct device *dev, uint8_t device_id,
                 uint8_t *buf, size_t len)
{
    struct spi_adv_data *data = dev->data;
    int ret;

    if (buf == NULL || len == 0 || device_id >= SPI_ADV_MAX_DEVICES) {
        return -EINVAL;
    }

    if (!data->devices[device_id].configured) {
        return -ENOENT;
    }

    struct spi_buf rx_buf = {
        .buf = buf,
        .len = len,
    };
    struct spi_buf_set rx_bufs = {
        .buffers = &rx_buf,
        .count = 1,
    };

    k_mutex_lock(&data->lock, K_FOREVER);
    ret = do_spi_transfer(dev, device_id, NULL, &rx_bufs);
    k_mutex_unlock(&data->lock);

    return ret == 0 ? len : ret;
}

int spi_adv_transceive(const struct device *dev, uint8_t device_id,
                       const uint8_t *tx_data, uint8_t *rx_data, size_t len)
{
    struct spi_adv_data *data = dev->data;
    int ret;

    if (len == 0 || device_id >= SPI_ADV_MAX_DEVICES) {
        return -EINVAL;
    }

    if (!data->devices[device_id].configured) {
        return -ENOENT;
    }

    struct spi_buf tx_buf = {
        .buf = (void *)tx_data,
        .len = len,
    };
    struct spi_buf rx_buf = {
        .buf = rx_data,
        .len = len,
    };
    struct spi_buf_set tx_bufs = {
        .buffers = &tx_buf,
        .count = 1,
    };
    struct spi_buf_set rx_bufs = {
        .buffers = &rx_buf,
        .count = 1,
    };

    k_mutex_lock(&data->lock, K_FOREVER);
    ret = do_spi_transfer(dev, device_id,
                          tx_data ? &tx_bufs : NULL,
                          rx_data ? &rx_bufs : NULL);
    k_mutex_unlock(&data->lock);

    return ret == 0 ? len : ret;
}

int spi_adv_reg_write(const struct device *dev, uint8_t device_id,
                      uint8_t reg_addr, const uint8_t *buf, size_t len)
{
    struct spi_adv_data *data = dev->data;
    int ret;

    if (buf == NULL || len == 0 || device_id >= SPI_ADV_MAX_DEVICES) {
        return -EINVAL;
    }

    if (!data->devices[device_id].configured) {
        return -ENOENT;
    }

    /* Most devices use bit 7 = 0 for write */
    uint8_t cmd = reg_addr & 0x7F;

    struct spi_buf tx_bufs_arr[2] = {
        { .buf = &cmd, .len = 1 },
        { .buf = (void *)buf, .len = len },
    };
    struct spi_buf_set tx_bufs = {
        .buffers = tx_bufs_arr,
        .count = 2,
    };

    k_mutex_lock(&data->lock, K_FOREVER);
    ret = do_spi_transfer(dev, device_id, &tx_bufs, NULL);
    k_mutex_unlock(&data->lock);

    return ret;
}

int spi_adv_reg_read(const struct device *dev, uint8_t device_id,
                     uint8_t reg_addr, uint8_t *buf, size_t len)
{
    struct spi_adv_data *data = dev->data;
    int ret;

    if (buf == NULL || len == 0 || device_id >= SPI_ADV_MAX_DEVICES) {
        return -EINVAL;
    }

    if (!data->devices[device_id].configured) {
        return -ENOENT;
    }

    /* Most devices use bit 7 = 1 for read */
    uint8_t cmd = reg_addr | 0x80;

    struct spi_buf tx_buf = { .buf = &cmd, .len = 1 };
    struct spi_buf rx_buf = { .buf = buf, .len = len };
    struct spi_buf_set tx_bufs = { .buffers = &tx_buf, .count = 1 };
    struct spi_buf_set rx_bufs = { .buffers = &rx_buf, .count = 1 };

    k_mutex_lock(&data->lock, K_FOREVER);

    /* Send command */
    ret = spi_write(data->spi_dev,
                    &data->devices[device_id].spi_cfg,
                    &tx_bufs);
    if (ret == 0) {
        /* Read data */
        ret = spi_read(data->spi_dev,
                       &data->devices[device_id].spi_cfg,
                       &rx_bufs);
    }

    k_mutex_unlock(&data->lock);

#ifdef CONFIG_SPI_ADVANCED_STATS
    if (ret == 0) {
        data->stats.transfers++;
        data->stats.tx_bytes += 1;
        data->stats.rx_bytes += len;
    } else {
        data->stats.errors++;
    }
#endif

    return ret;
}

int spi_adv_reg_write_byte(const struct device *dev, uint8_t device_id,
                           uint8_t reg_addr, uint8_t value)
{
    return spi_adv_reg_write(dev, device_id, reg_addr, &value, 1);
}

int spi_adv_reg_read_byte(const struct device *dev, uint8_t device_id,
                          uint8_t reg_addr, uint8_t *value)
{
    return spi_adv_reg_read(dev, device_id, reg_addr, value, 1);
}

int spi_adv_transfer(const struct device *dev, uint8_t device_id,
                     const struct spi_adv_transfer *xfer)
{
    if (xfer == NULL) {
        return -EINVAL;
    }

    switch (xfer->type) {
    case SPI_ADV_XFER_TX_ONLY:
        return spi_adv_write(dev, device_id, xfer->tx_buf, xfer->len);
    case SPI_ADV_XFER_RX_ONLY:
        return spi_adv_read(dev, device_id, xfer->rx_buf, xfer->len);
    case SPI_ADV_XFER_FULL_DUPLEX:
        return spi_adv_transceive(dev, device_id, xfer->tx_buf,
                                  xfer->rx_buf, xfer->len);
    default:
        return -EINVAL;
    }
}

int spi_adv_transfer_multi(const struct device *dev, uint8_t device_id,
                           const struct spi_adv_transfer *xfers, size_t count)
{
    int ret;
    size_t total = 0;

    if (xfers == NULL || count == 0) {
        return -EINVAL;
    }

    for (size_t i = 0; i < count; i++) {
        ret = spi_adv_transfer(dev, device_id, &xfers[i]);
        if (ret < 0) {
            return ret;
        }
        total += ret;
    }

    return total;
}

/* ========== Async API ========== */

#ifdef CONFIG_SPI_ADVANCED_ASYNC
static void spi_async_callback(const struct device *spi_dev,
                               int result, void *user_data)
{
    const struct device *dev = user_data;
    struct spi_adv_data *data = dev->data;

    data->async_pending = false;
    k_sem_give(&data->async_sem);

    if (data->async_callback) {
        data->async_callback(dev, result, data->async_user_data);
    }
}

int spi_adv_write_async(const struct device *dev, uint8_t device_id,
                        const uint8_t *buf, size_t len,
                        spi_adv_callback_t callback, void *user_data)
{
    struct spi_adv_data *data = dev->data;
    int ret;

    if (buf == NULL || len == 0 || device_id >= SPI_ADV_MAX_DEVICES) {
        return -EINVAL;
    }

    if (!data->devices[device_id].configured) {
        return -ENOENT;
    }

    k_mutex_lock(&data->lock, K_FOREVER);

    if (data->async_pending) {
        k_mutex_unlock(&data->lock);
        return -EBUSY;
    }

    data->async_callback = callback;
    data->async_user_data = user_data;
    data->async_pending = true;

    struct spi_buf tx_buf = {
        .buf = (void *)buf,
        .len = len,
    };
    struct spi_buf_set tx_bufs = {
        .buffers = &tx_buf,
        .count = 1,
    };

    ret = spi_transceive_cb(data->spi_dev,
                            &data->devices[device_id].spi_cfg,
                            &tx_bufs, NULL,
                            spi_async_callback, (void *)dev);

    if (ret < 0) {
        data->async_pending = false;
    }

    k_mutex_unlock(&data->lock);

    return ret;
}

int spi_adv_read_async(const struct device *dev, uint8_t device_id,
                       uint8_t *buf, size_t len,
                       spi_adv_callback_t callback, void *user_data)
{
    struct spi_adv_data *data = dev->data;
    int ret;

    if (buf == NULL || len == 0 || device_id >= SPI_ADV_MAX_DEVICES) {
        return -EINVAL;
    }

    if (!data->devices[device_id].configured) {
        return -ENOENT;
    }

    k_mutex_lock(&data->lock, K_FOREVER);

    if (data->async_pending) {
        k_mutex_unlock(&data->lock);
        return -EBUSY;
    }

    data->async_callback = callback;
    data->async_user_data = user_data;
    data->async_pending = true;

    struct spi_buf rx_buf = {
        .buf = buf,
        .len = len,
    };
    struct spi_buf_set rx_bufs = {
        .buffers = &rx_buf,
        .count = 1,
    };

    ret = spi_transceive_cb(data->spi_dev,
                            &data->devices[device_id].spi_cfg,
                            NULL, &rx_bufs,
                            spi_async_callback, (void *)dev);

    if (ret < 0) {
        data->async_pending = false;
    }

    k_mutex_unlock(&data->lock);

    return ret;
}

int spi_adv_transceive_async(const struct device *dev, uint8_t device_id,
                             const uint8_t *tx_data, uint8_t *rx_data,
                             size_t len, spi_adv_callback_t callback,
                             void *user_data)
{
    struct spi_adv_data *data = dev->data;
    int ret;

    if (len == 0 || device_id >= SPI_ADV_MAX_DEVICES) {
        return -EINVAL;
    }

    if (!data->devices[device_id].configured) {
        return -ENOENT;
    }

    k_mutex_lock(&data->lock, K_FOREVER);

    if (data->async_pending) {
        k_mutex_unlock(&data->lock);
        return -EBUSY;
    }

    data->async_callback = callback;
    data->async_user_data = user_data;
    data->async_pending = true;

    struct spi_buf tx_buf = { .buf = (void *)tx_data, .len = len };
    struct spi_buf rx_buf = { .buf = rx_data, .len = len };
    struct spi_buf_set tx_bufs = { .buffers = &tx_buf, .count = 1 };
    struct spi_buf_set rx_bufs = { .buffers = &rx_buf, .count = 1 };

    ret = spi_transceive_cb(data->spi_dev,
                            &data->devices[device_id].spi_cfg,
                            tx_data ? &tx_bufs : NULL,
                            rx_data ? &rx_bufs : NULL,
                            spi_async_callback, (void *)dev);

    if (ret < 0) {
        data->async_pending = false;
    }

    k_mutex_unlock(&data->lock);

    return ret;
}

int spi_adv_async_wait(const struct device *dev, k_timeout_t timeout)
{
    struct spi_adv_data *data = dev->data;

    return k_sem_take(&data->async_sem, timeout);
}

int spi_adv_async_cancel(const struct device *dev)
{
    struct spi_adv_data *data = dev->data;

    /* Zephyr SPI doesn't have a cancel API, so we just mark as not pending */
    data->async_pending = false;

    return 0;
}
#endif /* CONFIG_SPI_ADVANCED_ASYNC */

/* ========== Chip Select Control ========== */

int spi_adv_cs_select(const struct device *dev, uint8_t device_id)
{
    struct spi_adv_data *data = dev->data;
    struct spi_adv_device_config *cfg;

    if (device_id >= SPI_ADV_MAX_DEVICES) {
        return -EINVAL;
    }

    if (!data->devices[device_id].configured) {
        return -ENOENT;
    }

    cfg = &data->devices[device_id].config;

    if (cfg->cs_gpio.port == NULL) {
        return -ENOTSUP;
    }

    int val = (cfg->cs_ctrl == SPI_ADV_CS_ACTIVE_LOW) ? 0 : 1;
    return gpio_pin_set_dt(&cfg->cs_gpio, val);
}

int spi_adv_cs_release(const struct device *dev, uint8_t device_id)
{
    struct spi_adv_data *data = dev->data;
    struct spi_adv_device_config *cfg;

    if (device_id >= SPI_ADV_MAX_DEVICES) {
        return -EINVAL;
    }

    if (!data->devices[device_id].configured) {
        return -ENOENT;
    }

    cfg = &data->devices[device_id].config;

    if (cfg->cs_gpio.port == NULL) {
        return -ENOTSUP;
    }

    int val = (cfg->cs_ctrl == SPI_ADV_CS_ACTIVE_LOW) ? 1 : 0;
    return gpio_pin_set_dt(&cfg->cs_gpio, val);
}

/* ========== Statistics ========== */

#ifdef CONFIG_SPI_ADVANCED_STATS
int spi_adv_stats_get(const struct device *dev, struct spi_adv_stats *stats)
{
    struct spi_adv_data *data = dev->data;

    if (stats == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&data->lock, K_FOREVER);
    *stats = data->stats;
    k_mutex_unlock(&data->lock);

    return 0;
}

int spi_adv_stats_reset(const struct device *dev)
{
    struct spi_adv_data *data = dev->data;

    k_mutex_lock(&data->lock, K_FOREVER);
    memset(&data->stats, 0, sizeof(data->stats));
    k_mutex_unlock(&data->lock);

    return 0;
}
#endif

/* ========== Utility Functions ========== */

int spi_adv_set_frequency(const struct device *dev, uint8_t device_id,
                          uint32_t frequency)
{
    struct spi_adv_data *data = dev->data;

    if (device_id >= SPI_ADV_MAX_DEVICES) {
        return -EINVAL;
    }

    if (!data->devices[device_id].configured) {
        return -ENOENT;
    }

    k_mutex_lock(&data->lock, K_FOREVER);
    data->devices[device_id].config.frequency = frequency;
    data->devices[device_id].spi_cfg.frequency = frequency;
    k_mutex_unlock(&data->lock);

    return 0;
}

int spi_adv_set_mode(const struct device *dev, uint8_t device_id,
                     enum spi_adv_mode mode)
{
    struct spi_adv_data *data = dev->data;

    if (device_id >= SPI_ADV_MAX_DEVICES) {
        return -EINVAL;
    }

    if (!data->devices[device_id].configured) {
        return -ENOENT;
    }

    k_mutex_lock(&data->lock, K_FOREVER);
    data->devices[device_id].config.mode = mode;
    build_spi_config(&data->devices[device_id]);
    k_mutex_unlock(&data->lock);

    return 0;
}

bool spi_adv_device_is_configured(const struct device *dev, uint8_t device_id)
{
    struct spi_adv_data *data = dev->data;

    if (device_id >= SPI_ADV_MAX_DEVICES) {
        return false;
    }

    return data->devices[device_id].configured;
}

/* ========== Initialization ========== */

static int spi_adv_init(const struct device *dev)
{
    const struct spi_adv_cfg *config = dev->config;
    struct spi_adv_data *data = dev->data;

    LOG_DBG("Initializing %s", dev->name);

    if (!device_is_ready(config->spi_dev)) {
        LOG_ERR("SPI device not ready");
        return -ENODEV;
    }

    data->spi_dev = config->spi_dev;

    k_mutex_init(&data->lock);
    k_sem_init(&data->async_sem, 0, 1);

    memset(data->devices, 0, sizeof(data->devices));

#ifdef CONFIG_SPI_ADVANCED_STATS
    memset(&data->stats, 0, sizeof(data->stats));
#endif

    data->initialized = true;

    LOG_INF("Initialized (SPI: %s, max freq: %u Hz)",
            config->spi_dev->name, config->max_frequency);

    return 0;
}

/* ========== Device Instantiation ========== */

#define SPI_ADV_INIT(inst)                                                  \
    static struct spi_adv_data spi_adv_data_##inst;                         \
                                                                            \
    static const struct spi_adv_cfg spi_adv_cfg_##inst = {                  \
        .spi_dev = DEVICE_DT_GET(DT_INST_PHANDLE(inst, spi_device)),       \
        .max_frequency = DT_INST_PROP_OR(inst, max_frequency, 10000000),   \
    };                                                                      \
                                                                            \
    DEVICE_DT_INST_DEFINE(inst,                                            \
                          spi_adv_init,                                     \
                          NULL,                                             \
                          &spi_adv_data_##inst,                             \
                          &spi_adv_cfg_##inst,                              \
                          POST_KERNEL,                                      \
                          CONFIG_SPI_ADVANCED_INIT_PRIORITY,                \
                          NULL);

DT_INST_FOREACH_STATUS_OKAY(SPI_ADV_INIT)
