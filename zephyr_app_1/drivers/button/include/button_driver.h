/**
 * @file button_driver.h
 * @brief Button driver for Raspberry Pi Pico
 *
 * This driver provides a simple interface to handle button inputs
 * on the Raspberry Pi Pico board.
 */

#ifndef BUTTON_DRIVER_H
#define BUTTON_DRIVER_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

/**
 * @brief Button driver data structure
 */
struct button_driver_data {
    const struct device *gpio;
    struct gpio_callback gpio_cb;
    uint32_t pin;
    bool is_pressed;
};

/**
 * @brief Button driver configuration structure
 */
struct button_driver_config {
    struct gpio_dt_spec button;
};

/**
 * @brief Initialize the button driver
 *
 * @param dev Pointer to the device structure
 * @return 0 on success, negative errno on failure
 */
int button_driver_init(const struct device *dev);

/**
 * @brief Get the current button state
 *
 * @param dev Pointer to the device structure
 * @return true if button is pressed, false otherwise
 */
bool button_driver_is_pressed(const struct device *dev);

#endif /* BUTTON_DRIVER_H */ 