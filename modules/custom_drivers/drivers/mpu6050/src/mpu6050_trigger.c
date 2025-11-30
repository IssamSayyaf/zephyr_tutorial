/**
 * @file mpu6050_trigger.c
 * @brief MPU6050 Trigger (Interrupt) Support
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "mpu6050.h"

LOG_MODULE_DECLARE(mpu6050, CONFIG_MPU6050_CUSTOM_LOG_LEVEL);

/* Forward declarations for driver data structures */
struct mpu6050_drv_data {
    struct mpu6050_config config;
    const struct device *dev;
    struct gpio_callback gpio_cb;
    mpu6050_trigger_callback_t callback;
    void *user_data;

#ifdef CONFIG_MPU6050_TRIGGER_OWN_THREAD
    K_KERNEL_STACK_MEMBER(thread_stack, CONFIG_MPU6050_THREAD_STACK_SIZE);
    struct k_thread thread;
    struct k_sem trig_sem;
#elif defined(CONFIG_MPU6050_TRIGGER_GLOBAL_THREAD)
    struct k_work work;
#endif

    struct k_mutex lock;
    bool initialized;
};

struct mpu6050_drv_cfg {
    struct i2c_dt_spec i2c;
    struct gpio_dt_spec int_gpio;
    enum mpu6050_accel_fs accel_fs;
    enum mpu6050_gyro_fs gyro_fs;
    uint8_t sample_rate_div;
    uint8_t dlpf_cfg;
};

/* ========== Trigger Handling ========== */

static void mpu6050_handle_trigger(const struct device *dev)
{
    struct mpu6050_drv_data *data = dev->data;
    uint8_t int_status;

    /* Read interrupt status to clear it */
    mpu6050_reg_read(dev, MPU6050_REG_INT_STATUS, &int_status);

    LOG_DBG("Interrupt status: 0x%02x", int_status);

    /* Call user callback */
    if (data->callback) {
        data->callback(dev, data->user_data);
    }
}

#ifdef CONFIG_MPU6050_TRIGGER_OWN_THREAD
static void mpu6050_thread_main(void *p1, void *p2, void *p3)
{
    const struct device *dev = p1;
    struct mpu6050_drv_data *data = dev->data;

    while (1) {
        k_sem_take(&data->trig_sem, K_FOREVER);
        mpu6050_handle_trigger(dev);
    }
}
#elif defined(CONFIG_MPU6050_TRIGGER_GLOBAL_THREAD)
static void mpu6050_work_handler(struct k_work *work)
{
    struct mpu6050_drv_data *data =
        CONTAINER_OF(work, struct mpu6050_drv_data, work);

    mpu6050_handle_trigger(data->dev);
}
#endif

static void mpu6050_gpio_callback(const struct device *gpio_dev,
                                  struct gpio_callback *cb,
                                  uint32_t pins)
{
    struct mpu6050_drv_data *data =
        CONTAINER_OF(cb, struct mpu6050_drv_data, gpio_cb);

#ifdef CONFIG_MPU6050_TRIGGER_OWN_THREAD
    k_sem_give(&data->trig_sem);
#elif defined(CONFIG_MPU6050_TRIGGER_GLOBAL_THREAD)
    k_work_submit(&data->work);
#endif
}

int mpu6050_trigger_set(const struct device *dev,
                        mpu6050_trigger_callback_t callback,
                        void *user_data)
{
    struct mpu6050_drv_data *data = dev->data;
    int ret;

    k_mutex_lock(&data->lock, K_FOREVER);

    data->callback = callback;
    data->user_data = user_data;

    /* Enable/disable interrupt based on callback */
    if (callback) {
        /* Enable DATA_RDY interrupt */
        ret = mpu6050_reg_write(dev, MPU6050_REG_INT_ENABLE, BIT(0));
    } else {
        /* Disable all interrupts */
        ret = mpu6050_reg_write(dev, MPU6050_REG_INT_ENABLE, 0);
    }

    k_mutex_unlock(&data->lock);

    return ret;
}

int mpu6050_trigger_init(const struct device *dev)
{
    const struct mpu6050_drv_cfg *cfg = dev->config;
    struct mpu6050_drv_data *data = dev->data;
    int ret;

    data->dev = dev;

    /* Check if interrupt GPIO is configured */
    if (cfg->int_gpio.port == NULL) {
        LOG_INF("No interrupt GPIO configured");
        return 0;
    }

    if (!gpio_is_ready_dt(&cfg->int_gpio)) {
        LOG_ERR("Interrupt GPIO not ready");
        return -ENODEV;
    }

    /* Configure GPIO as input with interrupt */
    ret = gpio_pin_configure_dt(&cfg->int_gpio, GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("Failed to configure GPIO: %d", ret);
        return ret;
    }

    /* Setup GPIO callback */
    gpio_init_callback(&data->gpio_cb, mpu6050_gpio_callback,
                       BIT(cfg->int_gpio.pin));

    ret = gpio_add_callback(cfg->int_gpio.port, &data->gpio_cb);
    if (ret < 0) {
        LOG_ERR("Failed to add GPIO callback: %d", ret);
        return ret;
    }

    /* Enable GPIO interrupt */
    ret = gpio_pin_interrupt_configure_dt(&cfg->int_gpio,
                                          GPIO_INT_EDGE_TO_ACTIVE);
    if (ret < 0) {
        LOG_ERR("Failed to configure GPIO interrupt: %d", ret);
        return ret;
    }

    /* Configure MPU6050 interrupt pin (active high, push-pull, pulse) */
    ret = mpu6050_reg_write(dev, MPU6050_REG_INT_PIN_CFG, 0x00);
    if (ret < 0) {
        return ret;
    }

#ifdef CONFIG_MPU6050_TRIGGER_OWN_THREAD
    k_sem_init(&data->trig_sem, 0, 1);

    k_thread_create(&data->thread, data->thread_stack,
                    CONFIG_MPU6050_THREAD_STACK_SIZE,
                    mpu6050_thread_main, (void *)dev, NULL, NULL,
                    K_PRIO_COOP(CONFIG_MPU6050_THREAD_PRIORITY),
                    0, K_NO_WAIT);

    k_thread_name_set(&data->thread, "mpu6050_trigger");
#elif defined(CONFIG_MPU6050_TRIGGER_GLOBAL_THREAD)
    k_work_init(&data->work, mpu6050_work_handler);
#endif

    LOG_INF("Trigger initialized");

    return 0;
}
