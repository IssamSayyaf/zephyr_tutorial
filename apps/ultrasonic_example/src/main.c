/**
 * @file main.c
 * @brief Ultrasonic Sensor Example Application
 *
 * Demonstrates distance measurement with HC-SR04 sensor.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include "ultrasonic.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#if DT_NODE_EXISTS(DT_NODELABEL(ultrasonic0))
#define ULTRASONIC_NODE DT_NODELABEL(ultrasonic0)
#else
#error "No ultrasonic0 node found in devicetree"
#endif

#define DETECTION_THRESHOLD_CM 50

int main(void)
{
    const struct device *dev = DEVICE_DT_GET(ULTRASONIC_NODE);
    struct ultrasonic_result result;
    int ret;

    LOG_INF("Ultrasonic Sensor Example");
    LOG_INF("=========================");

    if (!device_is_ready(dev)) {
        LOG_ERR("Ultrasonic device not ready");
        return -ENODEV;
    }

    LOG_INF("Range: %u - %u cm",
            ultrasonic_get_min_range(dev),
            ultrasonic_get_max_range(dev));
    LOG_INF("");

    /* Set temperature for accurate speed of sound */
    ultrasonic_set_temperature(dev, 22.0f);

    LOG_INF("Measuring distances...");
    LOG_INF("Objects closer than %d cm will trigger alert", DETECTION_THRESHOLD_CM);
    LOG_INF("");

    while (1) {
        /* Take filtered measurement */
        ret = ultrasonic_measure_filtered(dev, 5, &result);

        if (ret < 0) {
            LOG_ERR("Measurement failed: %d", ret);
        } else if (result.valid) {
            LOG_INF("Distance: %3u cm (%4u mm) - Echo: %u us",
                    result.distance_cm,
                    result.distance_mm,
                    result.echo_time_us);

            /* Detection alert */
            if (result.distance_cm < DETECTION_THRESHOLD_CM) {
                LOG_WRN("*** OBJECT DETECTED! ***");
            }
        } else {
            enum ultrasonic_status status = ultrasonic_get_status(dev);
            switch (status) {
            case ULTRASONIC_STATUS_TIMEOUT:
                LOG_WRN("No echo - object too far or no object");
                break;
            case ULTRASONIC_STATUS_TOO_CLOSE:
                LOG_WRN("Object too close");
                break;
            default:
                LOG_WRN("Measurement error: %d", result.error);
            }
        }

        k_sleep(K_MSEC(500));
    }

    return 0;
}
