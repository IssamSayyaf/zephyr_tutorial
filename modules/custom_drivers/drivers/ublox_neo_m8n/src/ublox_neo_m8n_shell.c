/**
 * @file ublox_neo_m8n_shell.c
 * @brief Shell commands for uBlox NEO-M8N GPS driver
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/device.h>
#include <stdlib.h>

#include "ublox_neo_m8n.h"

#define GPS_DEVICE DT_NODELABEL(gps)

static const struct device *get_gps_dev(void)
{
#if DT_NODE_EXISTS(GPS_DEVICE)
    return DEVICE_DT_GET(GPS_DEVICE);
#else
    return NULL;
#endif
}

static int cmd_gps_start(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_gps_dev();
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "GPS device not available");
        return -ENODEV;
    }

    ret = gps_start(dev);
    if (ret < 0) {
        shell_error(sh, "Failed to start GPS: %d", ret);
        return ret;
    }

    shell_print(sh, "GPS started");
    return 0;
}

static int cmd_gps_stop(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_gps_dev();
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "GPS device not available");
        return -ENODEV;
    }

    ret = gps_stop(dev);
    if (ret < 0) {
        shell_error(sh, "Failed to stop GPS: %d", ret);
        return ret;
    }

    shell_print(sh, "GPS stopped");
    return 0;
}

static int cmd_gps_status(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_gps_dev();
    struct gps_data data;
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "GPS device not available");
        return -ENODEV;
    }

    ret = gps_get_data(dev, &data);
    if (ret < 0) {
        shell_error(sh, "Failed to get GPS data: %d", ret);
        return ret;
    }

    const char *fix_str[] = {"No Fix", "2D Fix", "3D Fix"};
    const char *quality_str[] = {"Invalid", "GPS", "DGPS", "PPS", "RTK",
                                  "Float RTK", "Estimated", "Manual", "Sim"};

    shell_print(sh, "GPS Status");
    shell_print(sh, "==========");
    shell_print(sh, "Running:     %s", gps_is_running(dev) ? "Yes" : "No");
    shell_print(sh, "Fix:         %s", data.fix_valid ? "Valid" : "Invalid");
    shell_print(sh, "Fix Type:    %s", fix_str[data.fix_type]);
    shell_print(sh, "Fix Quality: %s", quality_str[data.fix_quality]);
    shell_print(sh, "Satellites:  %u", data.satellites_used);
    shell_print(sh, "");

    if (data.position.valid) {
        shell_print(sh, "Position:");
        shell_print(sh, "  Latitude:  %.6f", data.position.latitude);
        shell_print(sh, "  Longitude: %.6f", data.position.longitude);
        shell_print(sh, "  Altitude:  %.1f m", (double)data.position.altitude);
    }

    if (data.velocity.valid) {
        shell_print(sh, "");
        shell_print(sh, "Velocity:");
        shell_print(sh, "  Speed:  %.2f m/s (%.2f km/h)",
                    (double)data.velocity.speed_mps,
                    (double)data.velocity.speed_kmh);
        shell_print(sh, "  Course: %.1f deg", (double)data.velocity.course);
    }

    if (data.time.valid) {
        shell_print(sh, "");
        shell_print(sh, "Time: %04u-%02u-%02u %02u:%02u:%02u UTC",
                    data.time.year, data.time.month, data.time.day,
                    data.time.hour, data.time.minute, data.time.second);
    }

    if (data.accuracy.valid) {
        shell_print(sh, "");
        shell_print(sh, "DOP:");
        shell_print(sh, "  HDOP: %.1f", (double)data.accuracy.hdop);
        shell_print(sh, "  VDOP: %.1f", (double)data.accuracy.vdop);
        shell_print(sh, "  PDOP: %.1f", (double)data.accuracy.pdop);
    }

    return 0;
}

static int cmd_gps_position(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_gps_dev();
    double lat, lon;
    float alt;
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "GPS device not available");
        return -ENODEV;
    }

    ret = gps_get_position(dev, &lat, &lon, &alt);
    if (ret < 0) {
        shell_print(sh, "No valid position fix");
        return 0;
    }

    shell_print(sh, "Position: %.6f, %.6f, %.1f m", lat, lon, (double)alt);

    return 0;
}

static int cmd_gps_time(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_gps_dev();
    struct gps_time time;
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "GPS device not available");
        return -ENODEV;
    }

    ret = gps_get_time(dev, &time);
    if (ret < 0) {
        shell_print(sh, "No valid time");
        return 0;
    }

    shell_print(sh, "Time: %04u-%02u-%02u %02u:%02u:%02u.%03u UTC",
                time.year, time.month, time.day,
                time.hour, time.minute, time.second, time.millisec);

    return 0;
}

static int cmd_gps_monitor(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_gps_dev();
    struct gps_data data;
    int count = 10;
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "GPS device not available");
        return -ENODEV;
    }

    if (argc > 1) {
        count = atoi(argv[1]);
    }

    if (!gps_is_running(dev)) {
        gps_start(dev);
    }

    shell_print(sh, "Monitoring GPS for %d samples...", count);
    shell_print(sh, "Fix | Sats | Lat | Lon | Alt | Speed");
    shell_print(sh, "----|------|-----|-----|-----|------");

    for (int i = 0; i < count; i++) {
        k_sleep(K_SECONDS(1));

        ret = gps_get_data(dev, &data);
        if (ret < 0) {
            continue;
        }

        if (data.position.valid) {
            shell_print(sh, " %s | %2u | %.4f | %.4f | %.0f | %.1f",
                        data.fix_type == GPS_FIX_3D ? "3D" :
                        data.fix_type == GPS_FIX_2D ? "2D" : "--",
                        data.satellites_used,
                        data.position.latitude,
                        data.position.longitude,
                        (double)data.position.altitude,
                        (double)data.velocity.speed_mps);
        } else {
            shell_print(sh, " -- | %2u | Waiting for fix...",
                        data.satellites_used);
        }
    }

    return 0;
}

static int cmd_gps_nmea(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_gps_dev();
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "GPS device not available");
        return -ENODEV;
    }

    if (argc < 2) {
        shell_print(sh, "Usage: gps nmea <sentence>");
        shell_print(sh, "  Sends NMEA command (without $ and checksum)");
        return -EINVAL;
    }

    ret = gps_send_nmea(dev, argv[1]);
    if (ret < 0) {
        shell_error(sh, "Failed to send: %d", ret);
        return ret;
    }

    shell_print(sh, "Sent: $%s*xx", argv[1]);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(gps_cmds,
    SHELL_CMD(start, NULL, "Start GPS acquisition", cmd_gps_start),
    SHELL_CMD(stop, NULL, "Stop GPS acquisition", cmd_gps_stop),
    SHELL_CMD(status, NULL, "Show GPS status", cmd_gps_status),
    SHELL_CMD(pos, NULL, "Show position", cmd_gps_position),
    SHELL_CMD(time, NULL, "Show GPS time", cmd_gps_time),
    SHELL_CMD(monitor, NULL, "Monitor GPS: monitor [count]", cmd_gps_monitor),
    SHELL_CMD(nmea, NULL, "Send NMEA command", cmd_gps_nmea),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(gps, &gps_cmds, "GPS module commands", NULL);
