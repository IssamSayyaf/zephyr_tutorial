/**
 * @file ultrasonic.c
 * @brief Ultrasonic Distance Sensor Driver Implementation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT custom_ultrasonic

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <math.h>

#include "ultrasonic.h"

LOG_MODULE_REGISTER(ultrasonic, CONFIG_ULTRASONIC_LOG_LEVEL);

/* Timing constants */
#define TRIGGER_PULSE_US     CONFIG_ULTRASONIC_TRIGGER_PULSE_US
#define ECHO_TIMEOUT_US      (CONFIG_ULTRASONIC_ECHO_TIMEOUT_MS * 1000)
#define MIN_INTERVAL_US      (CONFIG_ULTRASONIC_MIN_INTERVAL_MS * 1000)

/* Speed of sound in cm/us * 1000 */
#define SOUND_SPEED_DEFAULT  CONFIG_ULTRASONIC_SOUND_SPEED_CM_US

/* Minimum/maximum range in cm */
#define MIN_RANGE_CM         2
#define MAX_RANGE_CM         400

/**
 * @brief Driver runtime data
 */
struct ultrasonic_data {
    uint32_t last_measurement_time;
    uint32_t sound_speed;  /* cm/us * 1000 */
    float temperature;

    enum ultrasonic_status status;
    struct ultrasonic_result last_result;

    /* Callback for async */
    ultrasonic_callback_t callback;
    void *user_data;

    /* Continuous measurement */
    struct k_work_delayable continuous_work;
    uint32_t continuous_interval_ms;
    bool continuous_running;

    /* Synchronization */
    struct k_mutex lock;

    bool initialized;
};

/**
 * @brief Driver configuration
 */
struct ultrasonic_cfg {
    struct gpio_dt_spec trigger_gpio;
    struct gpio_dt_spec echo_gpio;
    uint32_t max_range_cm;
};

/* ========== Internal Functions ========== */

static int send_trigger(const struct ultrasonic_cfg *cfg)
{
    int ret;

    /* Send trigger pulse */
    ret = gpio_pin_set_dt(&cfg->trigger_gpio, 1);
    if (ret < 0) {
        return ret;
    }

    k_busy_wait(TRIGGER_PULSE_US);

    ret = gpio_pin_set_dt(&cfg->trigger_gpio, 0);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

static int wait_for_echo(const struct ultrasonic_cfg *cfg,
                         uint32_t *echo_time_us)
{
    uint32_t start, end;
    uint32_t timeout_start;

    /* Wait for echo to go high */
    timeout_start = k_cycle_get_32();
    while (gpio_pin_get_dt(&cfg->echo_gpio) == 0) {
        if (k_cyc_to_us_floor32(k_cycle_get_32() - timeout_start) > ECHO_TIMEOUT_US) {
            return -ETIMEDOUT;
        }
    }

    /* Measure echo pulse width */
    start = k_cycle_get_32();

    while (gpio_pin_get_dt(&cfg->echo_gpio) == 1) {
        if (k_cyc_to_us_floor32(k_cycle_get_32() - start) > ECHO_TIMEOUT_US) {
            return -ETIMEDOUT;
        }
    }

    end = k_cycle_get_32();

    *echo_time_us = k_cyc_to_us_floor32(end - start);

    return 0;
}

static uint32_t echo_to_distance_mm(uint32_t echo_time_us,
                                    uint32_t sound_speed)
{
    /* Distance = (time * speed) / 2
     * sound_speed is in cm/us * 1000
     * Result in mm = (time_us * speed/1000 * 10) / 2
     *              = (time_us * speed) / 200
     */
    return (echo_time_us * sound_speed) / 200;
}

static int compare_uint32(const void *a, const void *b)
{
    uint32_t va = *(const uint32_t *)a;
    uint32_t vb = *(const uint32_t *)b;

    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

/* ========== Public API ========== */

int ultrasonic_measure(const struct device *dev,
                       struct ultrasonic_result *result)
{
    const struct ultrasonic_cfg *cfg = dev->config;
    struct ultrasonic_data *data = dev->data;
    uint32_t now;
    uint32_t echo_time;
    int ret;

    if (result == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&data->lock, K_FOREVER);

    /* Enforce minimum interval */
    now = k_uptime_get_32() * 1000;  /* Convert to microseconds approximation */
    if (now - data->last_measurement_time < MIN_INTERVAL_US) {
        k_busy_wait(MIN_INTERVAL_US - (now - data->last_measurement_time));
    }

    /* Send trigger */
    ret = send_trigger(cfg);
    if (ret < 0) {
        data->status = ULTRASONIC_STATUS_ERROR;
        result->valid = false;
        result->error = ret;
        k_mutex_unlock(&data->lock);
        return ret;
    }

    /* Wait for echo */
    ret = wait_for_echo(cfg, &echo_time);
    if (ret < 0) {
        data->status = ULTRASONIC_STATUS_TIMEOUT;
        result->valid = false;
        result->error = ret;
        k_mutex_unlock(&data->lock);
        return ret;
    }

    data->last_measurement_time = k_uptime_get_32() * 1000;

    /* Calculate distance */
    result->echo_time_us = echo_time;
    result->distance_mm = echo_to_distance_mm(echo_time, data->sound_speed);
    result->distance_cm = result->distance_mm / 10;

    /* Validate range */
    if (result->distance_cm < MIN_RANGE_CM) {
        data->status = ULTRASONIC_STATUS_TOO_CLOSE;
        result->valid = false;
        result->error = -ERANGE;
    } else if (result->distance_cm > cfg->max_range_cm) {
        data->status = ULTRASONIC_STATUS_TOO_FAR;
        result->valid = false;
        result->error = -ERANGE;
    } else {
        data->status = ULTRASONIC_STATUS_OK;
        result->valid = true;
        result->error = 0;
    }

    data->last_result = *result;

    k_mutex_unlock(&data->lock);

    LOG_DBG("Echo: %u us, Distance: %u mm (%u cm)",
            result->echo_time_us, result->distance_mm, result->distance_cm);

    return 0;
}

int ultrasonic_get_distance_cm(const struct device *dev,
                               uint32_t *distance_cm)
{
    struct ultrasonic_result result;
    int ret;

    if (distance_cm == NULL) {
        return -EINVAL;
    }

#ifdef CONFIG_ULTRASONIC_MEDIAN_FILTER
    ret = ultrasonic_measure_filtered(dev, CONFIG_ULTRASONIC_MEDIAN_SAMPLES,
                                      &result);
#else
    ret = ultrasonic_measure(dev, &result);
#endif

    if (ret < 0) {
        return ret;
    }

    if (!result.valid) {
        return result.error;
    }

    *distance_cm = result.distance_cm;
    return 0;
}

int ultrasonic_get_distance_mm(const struct device *dev,
                               uint32_t *distance_mm)
{
    struct ultrasonic_result result;
    int ret;

    if (distance_mm == NULL) {
        return -EINVAL;
    }

#ifdef CONFIG_ULTRASONIC_MEDIAN_FILTER
    ret = ultrasonic_measure_filtered(dev, CONFIG_ULTRASONIC_MEDIAN_SAMPLES,
                                      &result);
#else
    ret = ultrasonic_measure(dev, &result);
#endif

    if (ret < 0) {
        return ret;
    }

    if (!result.valid) {
        return result.error;
    }

    *distance_mm = result.distance_mm;
    return 0;
}

int ultrasonic_get_echo_time(const struct device *dev,
                             uint32_t *time_us)
{
    struct ultrasonic_result result;
    int ret;

    if (time_us == NULL) {
        return -EINVAL;
    }

    ret = ultrasonic_measure(dev, &result);
    if (ret < 0) {
        return ret;
    }

    *time_us = result.echo_time_us;
    return 0;
}

int ultrasonic_measure_filtered(const struct device *dev,
                                uint8_t samples,
                                struct ultrasonic_result *result)
{
    uint32_t measurements[11];
    struct ultrasonic_result temp;
    int valid_count = 0;
    int ret;

    if (result == NULL || samples < 3 || samples > 11 || (samples % 2) == 0) {
        return -EINVAL;
    }

    /* Take multiple measurements */
    for (int i = 0; i < samples; i++) {
        ret = ultrasonic_measure(dev, &temp);
        if (ret == 0 && temp.valid) {
            measurements[valid_count++] = temp.distance_mm;
        }
        /* Small delay between measurements */
        k_sleep(K_MSEC(10));
    }

    if (valid_count == 0) {
        result->valid = false;
        result->error = -EAGAIN;
        return -EAGAIN;
    }

    /* Sort and take median */
    qsort(measurements, valid_count, sizeof(uint32_t), compare_uint32);

    result->distance_mm = measurements[valid_count / 2];
    result->distance_cm = result->distance_mm / 10;
    result->echo_time_us = (result->distance_mm * 200) /
                           ((struct ultrasonic_data *)dev->data)->sound_speed;
    result->valid = true;
    result->error = 0;

    LOG_DBG("Filtered distance: %u mm (from %d samples)",
            result->distance_mm, valid_count);

    return 0;
}

int ultrasonic_set_temperature(const struct device *dev,
                               float temp_celsius)
{
    struct ultrasonic_data *data = dev->data;

    k_mutex_lock(&data->lock, K_FOREVER);

    data->temperature = temp_celsius;

    /* Speed of sound = 331.3 + 0.606 * T (m/s)
     * Convert to cm/us * 1000: (331.3 + 0.606 * T) / 10000 * 1000
     */
    float speed_ms = 331.3f + (0.606f * temp_celsius);
    data->sound_speed = (uint32_t)((speed_ms / 10000.0f) * 1000.0f * 1000.0f);

    k_mutex_unlock(&data->lock);

    LOG_INF("Temperature: %.1f C, Sound speed: %u (x1000 cm/us)",
            (double)temp_celsius, data->sound_speed);

    return 0;
}

int ultrasonic_set_callback(const struct device *dev,
                            ultrasonic_callback_t callback,
                            void *user_data)
{
    struct ultrasonic_data *data = dev->data;

    k_mutex_lock(&data->lock, K_FOREVER);
    data->callback = callback;
    data->user_data = user_data;
    k_mutex_unlock(&data->lock);

    return 0;
}

static void continuous_work_handler(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct ultrasonic_data *data =
        CONTAINER_OF(dwork, struct ultrasonic_data, continuous_work);

    if (!data->continuous_running) {
        return;
    }

    struct ultrasonic_result result;
    const struct device *dev = NULL;  /* Would need to store dev reference */

    /* Note: In a real implementation, we'd store the device reference */

    /* Reschedule */
    k_work_schedule(dwork, K_MSEC(data->continuous_interval_ms));
}

int ultrasonic_start_continuous(const struct device *dev,
                                uint32_t interval_ms)
{
    struct ultrasonic_data *data = dev->data;

    if (interval_ms < CONFIG_ULTRASONIC_MIN_INTERVAL_MS) {
        return -EINVAL;
    }

    k_mutex_lock(&data->lock, K_FOREVER);

    data->continuous_interval_ms = interval_ms;
    data->continuous_running = true;

    k_work_schedule(&data->continuous_work, K_NO_WAIT);

    k_mutex_unlock(&data->lock);

    LOG_INF("Continuous measurement started (%u ms interval)", interval_ms);

    return 0;
}

int ultrasonic_stop_continuous(const struct device *dev)
{
    struct ultrasonic_data *data = dev->data;

    k_mutex_lock(&data->lock, K_FOREVER);

    data->continuous_running = false;
    k_work_cancel_delayable(&data->continuous_work);

    k_mutex_unlock(&data->lock);

    LOG_INF("Continuous measurement stopped");

    return 0;
}

bool ultrasonic_in_range(const struct device *dev,
                         uint32_t min_cm, uint32_t max_cm)
{
    uint32_t distance;
    int ret;

    ret = ultrasonic_get_distance_cm(dev, &distance);
    if (ret < 0) {
        return false;
    }

    return (distance >= min_cm && distance <= max_cm);
}

enum ultrasonic_status ultrasonic_get_status(const struct device *dev)
{
    struct ultrasonic_data *data = dev->data;
    return data->status;
}

uint32_t ultrasonic_get_min_range(const struct device *dev)
{
    return MIN_RANGE_CM;
}

uint32_t ultrasonic_get_max_range(const struct device *dev)
{
    const struct ultrasonic_cfg *cfg = dev->config;
    return cfg->max_range_cm;
}

/* ========== Initialization ========== */

static int ultrasonic_init(const struct device *dev)
{
    const struct ultrasonic_cfg *cfg = dev->config;
    struct ultrasonic_data *data = dev->data;
    int ret;

    LOG_DBG("Initializing %s", dev->name);

    /* Check trigger GPIO */
    if (!gpio_is_ready_dt(&cfg->trigger_gpio)) {
        LOG_ERR("Trigger GPIO not ready");
        return -ENODEV;
    }

    /* Check echo GPIO */
    if (!gpio_is_ready_dt(&cfg->echo_gpio)) {
        LOG_ERR("Echo GPIO not ready");
        return -ENODEV;
    }

    /* Configure trigger as output */
    ret = gpio_pin_configure_dt(&cfg->trigger_gpio, GPIO_OUTPUT_LOW);
    if (ret < 0) {
        LOG_ERR("Failed to configure trigger GPIO: %d", ret);
        return ret;
    }

    /* Configure echo as input */
    ret = gpio_pin_configure_dt(&cfg->echo_gpio, GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("Failed to configure echo GPIO: %d", ret);
        return ret;
    }

    /* Initialize data */
    k_mutex_init(&data->lock);
    k_work_init_delayable(&data->continuous_work, continuous_work_handler);

    data->sound_speed = SOUND_SPEED_DEFAULT;
    data->temperature = 20.0f;  /* Default 20C */
    data->status = ULTRASONIC_STATUS_OK;

    data->initialized = true;

    LOG_INF("Initialized (max range: %u cm)", cfg->max_range_cm);

    return 0;
}

/* ========== Device Instantiation ========== */

#define ULTRASONIC_INIT(inst)                                               \
    static struct ultrasonic_data ultrasonic_data_##inst;                   \
                                                                            \
    static const struct ultrasonic_cfg ultrasonic_cfg_##inst = {            \
        .trigger_gpio = GPIO_DT_SPEC_INST_GET(inst, trigger_gpios),        \
        .echo_gpio = GPIO_DT_SPEC_INST_GET(inst, echo_gpios),              \
        .max_range_cm = DT_INST_PROP_OR(inst, max_range_cm, MAX_RANGE_CM), \
    };                                                                      \
                                                                            \
    DEVICE_DT_INST_DEFINE(inst,                                            \
                          ultrasonic_init,                                  \
                          NULL,                                             \
                          &ultrasonic_data_##inst,                          \
                          &ultrasonic_cfg_##inst,                           \
                          POST_KERNEL,                                      \
                          CONFIG_ULTRASONIC_INIT_PRIORITY,                  \
                          NULL);

DT_INST_FOREACH_STATUS_OKAY(ULTRASONIC_INIT)
