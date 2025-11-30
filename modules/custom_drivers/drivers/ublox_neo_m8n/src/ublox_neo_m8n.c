/**
 * @file ublox_neo_m8n.c
 * @brief uBlox NEO-M8N GPS Driver Implementation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT u_blox_neo_m8n

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdio.h>

#include "ublox_neo_m8n.h"

LOG_MODULE_REGISTER(ublox_neo_m8n, CONFIG_UBLOX_NEO_M8N_LOG_LEVEL);

#define RX_BUFFER_SIZE CONFIG_UBLOX_NEO_M8N_RX_BUFFER_SIZE

/* External parser function */
extern int nmea_parse_sentence(const char *sentence, struct gps_data *data);

/**
 * @brief Driver runtime data
 */
struct gps_drv_data {
    const struct device *uart_dev;

    /* NMEA reception */
    char rx_buffer[RX_BUFFER_SIZE];
    size_t rx_idx;
    bool rx_overflow;

    /* Parsed data */
    struct gps_data data;

    /* Callback */
    gps_callback_t callback;
    void *user_data;

    /* Thread */
    K_KERNEL_STACK_MEMBER(thread_stack, CONFIG_UBLOX_NEO_M8N_THREAD_STACK_SIZE);
    struct k_thread thread;
    struct k_sem rx_sem;

    /* Synchronization */
    struct k_mutex lock;

    /* State */
    bool running;
    bool initialized;
};

/**
 * @brief Driver configuration
 */
struct gps_drv_cfg {
    const struct device *uart_dev;
    uint32_t baudrate;
};

/* ========== UART Handling ========== */

static void uart_rx_handler(const struct device *uart_dev, void *user_data)
{
    const struct device *dev = user_data;
    struct gps_drv_data *data = dev->data;
    uint8_t c;

    while (uart_irq_update(uart_dev) && uart_irq_rx_ready(uart_dev)) {
        if (uart_fifo_read(uart_dev, &c, 1) != 1) {
            continue;
        }

        if (c == '$') {
            /* Start of new sentence */
            data->rx_idx = 0;
            data->rx_overflow = false;
        }

        if (!data->rx_overflow && data->rx_idx < RX_BUFFER_SIZE - 1) {
            data->rx_buffer[data->rx_idx++] = c;

            if (c == '\n') {
                /* End of sentence */
                data->rx_buffer[data->rx_idx] = '\0';
                k_sem_give(&data->rx_sem);
            }
        } else {
            data->rx_overflow = true;
        }
    }
}

/* ========== Processing Thread ========== */

static void gps_thread_entry(void *p1, void *p2, void *p3)
{
    const struct device *dev = p1;
    struct gps_drv_data *data = dev->data;
    char sentence[RX_BUFFER_SIZE];

    while (1) {
        /* Wait for complete sentence */
        k_sem_take(&data->rx_sem, K_FOREVER);

        if (!data->running) {
            continue;
        }

        /* Copy sentence under lock */
        k_mutex_lock(&data->lock, K_FOREVER);
        strcpy(sentence, data->rx_buffer);
        k_mutex_unlock(&data->lock);

        /* Parse sentence */
        int ret = nmea_parse_sentence(sentence, &data->data);
        if (ret == 0) {
            data->data.timestamp = k_uptime_get_32();

            /* Call user callback */
            if (data->callback) {
                data->callback(dev, &data->data, data->user_data);
            }

            LOG_DBG("Parsed: Fix=%d, Sats=%d",
                    data->data.fix_valid, data->data.satellites_used);
        }
    }
}

/* ========== Public API ========== */

int gps_start(const struct device *dev)
{
    struct gps_drv_data *data = dev->data;
    const struct gps_drv_cfg *cfg = dev->config;

    if (!data->initialized) {
        return -ENODEV;
    }

    k_mutex_lock(&data->lock, K_FOREVER);

    if (data->running) {
        k_mutex_unlock(&data->lock);
        return 0;
    }

    /* Enable UART RX interrupt */
    uart_irq_callback_user_data_set(cfg->uart_dev, uart_rx_handler, (void *)dev);
    uart_irq_rx_enable(cfg->uart_dev);

    data->running = true;

    k_mutex_unlock(&data->lock);

    LOG_INF("GPS started");
    return 0;
}

int gps_stop(const struct device *dev)
{
    struct gps_drv_data *data = dev->data;
    const struct gps_drv_cfg *cfg = dev->config;

    k_mutex_lock(&data->lock, K_FOREVER);

    if (!data->running) {
        k_mutex_unlock(&data->lock);
        return 0;
    }

    uart_irq_rx_disable(cfg->uart_dev);
    data->running = false;

    k_mutex_unlock(&data->lock);

    LOG_INF("GPS stopped");
    return 0;
}

bool gps_is_running(const struct device *dev)
{
    struct gps_drv_data *data = dev->data;
    return data->running;
}

int gps_get_data(const struct device *dev, struct gps_data *out)
{
    struct gps_drv_data *data = dev->data;

    if (out == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&data->lock, K_FOREVER);
    *out = data->data;
    k_mutex_unlock(&data->lock);

    return 0;
}

int gps_get_position(const struct device *dev,
                     double *lat, double *lon, float *alt)
{
    struct gps_drv_data *data = dev->data;

    k_mutex_lock(&data->lock, K_FOREVER);

    if (!data->data.position.valid) {
        k_mutex_unlock(&data->lock);
        return -EAGAIN;
    }

    if (lat) *lat = data->data.position.latitude;
    if (lon) *lon = data->data.position.longitude;
    if (alt) *alt = data->data.position.altitude;

    k_mutex_unlock(&data->lock);

    return 0;
}

int gps_get_velocity(const struct device *dev,
                     float *speed, float *course)
{
    struct gps_drv_data *data = dev->data;

    k_mutex_lock(&data->lock, K_FOREVER);

    if (!data->data.velocity.valid) {
        k_mutex_unlock(&data->lock);
        return -EAGAIN;
    }

    if (speed) *speed = data->data.velocity.speed_mps;
    if (course) *course = data->data.velocity.course;

    k_mutex_unlock(&data->lock);

    return 0;
}

int gps_get_time(const struct device *dev, struct gps_time *time)
{
    struct gps_drv_data *data = dev->data;

    if (time == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&data->lock, K_FOREVER);

    if (!data->data.time.valid) {
        k_mutex_unlock(&data->lock);
        return -EAGAIN;
    }

    *time = data->data.time;

    k_mutex_unlock(&data->lock);

    return 0;
}

int gps_get_fix_status(const struct device *dev,
                       enum gps_fix_type *fix_type,
                       enum gps_fix_quality *quality,
                       uint8_t *satellites)
{
    struct gps_drv_data *data = dev->data;

    k_mutex_lock(&data->lock, K_FOREVER);

    if (fix_type) *fix_type = data->data.fix_type;
    if (quality) *quality = data->data.fix_quality;
    if (satellites) *satellites = data->data.satellites_used;

    k_mutex_unlock(&data->lock);

    return 0;
}

bool gps_has_fix(const struct device *dev)
{
    struct gps_drv_data *data = dev->data;
    return data->data.fix_valid;
}

int gps_set_callback(const struct device *dev,
                     gps_callback_t callback,
                     void *user_data)
{
    struct gps_drv_data *data = dev->data;

    k_mutex_lock(&data->lock, K_FOREVER);
    data->callback = callback;
    data->user_data = user_data;
    k_mutex_unlock(&data->lock);

    return 0;
}

int gps_send_nmea(const struct device *dev, const char *nmea)
{
    const struct gps_drv_cfg *cfg = dev->config;
    char buffer[128];
    uint8_t checksum = 0;
    size_t len;

    if (nmea == NULL) {
        return -EINVAL;
    }

    /* Calculate checksum */
    for (const char *p = nmea; *p; p++) {
        checksum ^= *p;
    }

    /* Format complete sentence */
    len = snprintf(buffer, sizeof(buffer), "$%s*%02X\r\n", nmea, checksum);
    if (len >= sizeof(buffer)) {
        return -EINVAL;
    }

    /* Send via UART */
    for (size_t i = 0; i < len; i++) {
        uart_poll_out(cfg->uart_dev, buffer[i]);
    }

    LOG_DBG("Sent: %s", buffer);
    return 0;
}

int gps_set_update_rate(const struct device *dev, uint8_t rate_hz)
{
    char cmd[32];

    if (rate_hz < 1 || rate_hz > 10) {
        return -EINVAL;
    }

    /* UBX CFG-RATE command would go here */
    /* For NMEA-only, we can use PMTK command (MTK chipset) */
    /* NEO-M8N uses UBX protocol for configuration */

    uint16_t period_ms = 1000 / rate_hz;
    snprintf(cmd, sizeof(cmd), "PUBX,40,GGA,0,%d,0,0", period_ms);

    return gps_send_nmea(dev, cmd);
}

int gps_cold_start(const struct device *dev)
{
    /* UBX CFG-RST command for cold start */
    return gps_send_nmea(dev, "PUBX,00");
}

int gps_warm_start(const struct device *dev)
{
    /* UBX CFG-RST command for warm start */
    return gps_send_nmea(dev, "PUBX,00");
}

int gps_hot_start(const struct device *dev)
{
    /* UBX CFG-RST command for hot start */
    return gps_send_nmea(dev, "PUBX,00");
}

int gps_configure_gnss(const struct device *dev,
                       bool gps, bool glonass,
                       bool galileo, bool beidou)
{
    /* Would require UBX protocol implementation */
    return -ENOTSUP;
}

/* ========== Initialization ========== */

static int gps_init(const struct device *dev)
{
    const struct gps_drv_cfg *cfg = dev->config;
    struct gps_drv_data *data = dev->data;

    LOG_DBG("Initializing %s", dev->name);

    /* Check UART device */
    if (!device_is_ready(cfg->uart_dev)) {
        LOG_ERR("UART device not ready");
        return -ENODEV;
    }

    data->uart_dev = cfg->uart_dev;

    /* Initialize synchronization */
    k_mutex_init(&data->lock);
    k_sem_init(&data->rx_sem, 0, 1);

    /* Initialize buffers */
    data->rx_idx = 0;
    memset(&data->data, 0, sizeof(data->data));

    /* Create processing thread */
    k_thread_create(&data->thread, data->thread_stack,
                    CONFIG_UBLOX_NEO_M8N_THREAD_STACK_SIZE,
                    gps_thread_entry, (void *)dev, NULL, NULL,
                    K_PRIO_COOP(CONFIG_UBLOX_NEO_M8N_THREAD_PRIORITY),
                    0, K_NO_WAIT);

    k_thread_name_set(&data->thread, "gps_parser");

    data->running = false;
    data->initialized = true;

    LOG_INF("Initialized (UART: %s, baud: %u)",
            cfg->uart_dev->name, cfg->baudrate);

    return 0;
}

/* ========== Device Instantiation ========== */

#define GPS_INIT(inst)                                                      \
    static struct gps_drv_data gps_data_##inst;                             \
                                                                            \
    static const struct gps_drv_cfg gps_cfg_##inst = {                      \
        .uart_dev = DEVICE_DT_GET(DT_INST_BUS(inst)),                      \
        .baudrate = DT_INST_PROP_OR(inst, current_speed,                   \
                                    CONFIG_UBLOX_NEO_M8N_DEFAULT_BAUDRATE), \
    };                                                                      \
                                                                            \
    DEVICE_DT_INST_DEFINE(inst,                                            \
                          gps_init,                                         \
                          NULL,                                             \
                          &gps_data_##inst,                                 \
                          &gps_cfg_##inst,                                  \
                          POST_KERNEL,                                      \
                          CONFIG_UBLOX_NEO_M8N_INIT_PRIORITY,               \
                          NULL);

DT_INST_FOREACH_STATUS_OKAY(GPS_INIT)
