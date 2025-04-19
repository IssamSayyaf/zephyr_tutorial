/**
 * @file main.c
 * @brief Sample application using the button driver
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "button_driver.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

#define SLEEP_TIME_MS   100

void main(void)
{
	const struct device *button = DEVICE_DT_GET(DT_ALIAS(button0));
	bool last_state = false;
	bool current_state;

	if (!device_is_ready(button)) {
		LOG_ERR("Button device not ready");
		return;
	}

	LOG_INF("Button application started");

	while (1) {
		current_state = button_driver_is_pressed(button);
		
		if (current_state != last_state) {
			LOG_INF("Button state changed: %s", 
				   current_state ? "pressed" : "released");
			last_state = current_state;
		}

		k_msleep(SLEEP_TIME_MS);
	}
}
