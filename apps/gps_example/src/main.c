/**
 * @file main.c
 * @brief GPS (uBlox NEO-M8N) Example Application
 *
 * Demonstrates GPS data acquisition and parsing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include "ublox_neo_m8n.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#if DT_NODE_EXISTS(DT_NODELABEL(gps))
#define GPS_NODE DT_NODELABEL(gps)
#else
#error "No gps node found in devicetree"
#endif

static void gps_data_callback(const struct device *dev,
                              const struct gps_data *data,
                              void *user_data)
{
    if (data->fix_valid) {
        LOG_INF("GPS Fix: Lat=%.6f, Lon=%.6f, Alt=%.1f m",
                data->position.latitude,
                data->position.longitude,
                (double)data->position.altitude);

        if (data->velocity.valid) {
            LOG_INF("Speed: %.1f m/s (%.1f km/h), Course: %.1f deg",
                    (double)data->velocity.speed_mps,
                    (double)data->velocity.speed_kmh,
                    (double)data->velocity.course);
        }
    }
}

int main(void)
{
    const struct device *dev = DEVICE_DT_GET(GPS_NODE);
    struct gps_data data;
    int ret;

    LOG_INF("GPS Example Application");
    LOG_INF("=======================");

    if (!device_is_ready(dev)) {
        LOG_ERR("GPS device not ready");
        return -ENODEV;
    }

    /* Set callback for new data */
    gps_set_callback(dev, gps_data_callback, NULL);

    /* Start GPS acquisition */
    ret = gps_start(dev);
    if (ret < 0) {
        LOG_ERR("Failed to start GPS: %d", ret);
        return ret;
    }

    LOG_INF("GPS started, waiting for fix...");
    LOG_INF("");

    while (1) {
        ret = gps_get_data(dev, &data);
        if (ret == 0) {
            const char *fix_str[] = {"No Fix", "2D Fix", "3D Fix"};

            LOG_INF("Status: %s, Satellites: %u",
                    fix_str[data.fix_type],
                    data.satellites_used);

            if (data.time.valid) {
                LOG_INF("Time: %04u-%02u-%02u %02u:%02u:%02u UTC",
                        data.time.year, data.time.month, data.time.day,
                        data.time.hour, data.time.minute, data.time.second);
            }

            if (data.accuracy.valid) {
                LOG_INF("HDOP: %.1f, VDOP: %.1f, PDOP: %.1f",
                        (double)data.accuracy.hdop,
                        (double)data.accuracy.vdop,
                        (double)data.accuracy.pdop);
            }

            LOG_INF("");
        }

        k_sleep(K_SECONDS(2));
    }

    return 0;
}
