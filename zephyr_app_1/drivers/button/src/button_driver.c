/**
 * @file button_driver.c
 * @brief Button driver implementation for Raspberry Pi Pico
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "button_driver.h"

LOG_MODULE_REGISTER(button_driver, CONFIG_LOG_DEFAULT_LEVEL);

static void button_callback(const struct device *dev, struct gpio_callback *cb,
                          uint32_t pins)
{
    struct button_driver_data *data = CONTAINER_OF(cb, struct button_driver_data, gpio_cb);
    data->is_pressed = gpio_pin_get(dev, data->pin) == 0;
    LOG_INF("Button %s", data->is_pressed ? "pressed" : "released");
}

int button_driver_init(const struct device *dev)
{
    const struct button_driver_config *config = dev->config;
    struct button_driver_data *data = dev->data;
    int ret;

    if (!device_is_ready(config->button.port)) {
        LOG_ERR("GPIO device not ready");
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&config->button, GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("Failed to configure GPIO pin");
        return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(&config->button, GPIO_INT_EDGE_BOTH);
    if (ret < 0) {
        LOG_ERR("Failed to configure GPIO interrupt");
        return ret;
    }

    data->gpio = config->button.port;
    data->pin = config->button.pin;
    data->is_pressed = false;

    gpio_init_callback(&data->gpio_cb, button_callback, BIT(data->pin));
    gpio_add_callback(data->gpio, &data->gpio_cb);

    LOG_INF("Button driver initialized");
    return 0;
}

bool button_driver_is_pressed(const struct device *dev)
{
    struct button_driver_data *data = dev->data;
    return data->is_pressed;
}

#define BUTTON_DRIVER_INIT(inst)                                              \
    static struct button_driver_data button_data_##inst;                     \
                                                                             \
    static const struct button_driver_config button_config_##inst = {        \
        .button = GPIO_DT_SPEC_INST_GET(inst, gpios),                       \
    };                                                                       \
                                                                             \
    DEVICE_DT_INST_DEFINE(inst,                                             \
                         button_driver_init,                                 \
                         NULL,                                              \
                         &button_data_##inst,                               \
                         &button_config_##inst,                             \
                         POST_KERNEL,                                       \
                         CONFIG_KERNEL_INIT_PRIORITY_DEVICE,                \
                         NULL);

DT_INST_FOREACH_STATUS_OKAY(BUTTON_DRIVER_INIT) 