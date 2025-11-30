/**
 * @file ultrasonic_shell.c
 * @brief Shell commands for Ultrasonic sensor driver
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/device.h>
#include <stdlib.h>

#include "ultrasonic.h"

#define ULTRASONIC_DEVICE DT_NODELABEL(ultrasonic0)

static const struct device *get_ultrasonic_dev(void)
{
#if DT_NODE_EXISTS(ULTRASONIC_DEVICE)
    return DEVICE_DT_GET(ULTRASONIC_DEVICE);
#else
    return NULL;
#endif
}

static int cmd_ultrasonic_measure(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_ultrasonic_dev();
    struct ultrasonic_result result;
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "Ultrasonic device not available");
        return -ENODEV;
    }

    ret = ultrasonic_measure(dev, &result);
    if (ret < 0) {
        shell_error(sh, "Measurement failed: %d", ret);
        return ret;
    }

    if (result.valid) {
        shell_print(sh, "Distance: %u cm (%u mm)",
                    result.distance_cm, result.distance_mm);
        shell_print(sh, "Echo time: %u us", result.echo_time_us);
    } else {
        const char *status_str[] = {
            "OK", "Timeout", "Too close", "Too far", "Error"
        };
        shell_print(sh, "Measurement invalid: %s",
                    status_str[ultrasonic_get_status(dev)]);
    }

    return 0;
}

static int cmd_ultrasonic_filtered(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_ultrasonic_dev();
    struct ultrasonic_result result;
    int samples = 5;
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "Ultrasonic device not available");
        return -ENODEV;
    }

    if (argc > 1) {
        samples = atoi(argv[1]);
        if (samples < 3 || samples > 11 || (samples % 2) == 0) {
            shell_error(sh, "Samples must be odd number 3-11");
            return -EINVAL;
        }
    }

    shell_print(sh, "Taking %d samples with median filter...", samples);

    ret = ultrasonic_measure_filtered(dev, samples, &result);
    if (ret < 0) {
        shell_error(sh, "Measurement failed: %d", ret);
        return ret;
    }

    if (result.valid) {
        shell_print(sh, "Filtered distance: %u cm (%u mm)",
                    result.distance_cm, result.distance_mm);
    } else {
        shell_print(sh, "Measurement invalid");
    }

    return 0;
}

static int cmd_ultrasonic_monitor(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_ultrasonic_dev();
    int count = 10;
    int interval = 500;
    uint32_t distance;
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "Ultrasonic device not available");
        return -ENODEV;
    }

    if (argc > 1) {
        count = atoi(argv[1]);
    }
    if (argc > 2) {
        interval = atoi(argv[2]);
    }

    shell_print(sh, "Monitoring for %d samples (%d ms interval)...", count, interval);

    for (int i = 0; i < count; i++) {
        ret = ultrasonic_get_distance_cm(dev, &distance);
        if (ret == 0) {
            shell_print(sh, "[%3d] Distance: %3u cm", i + 1, distance);
        } else {
            shell_print(sh, "[%3d] Error: %d", i + 1, ret);
        }

        if (i < count - 1) {
            k_sleep(K_MSEC(interval));
        }
    }

    return 0;
}

static int cmd_ultrasonic_temp(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_ultrasonic_dev();
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "Ultrasonic device not available");
        return -ENODEV;
    }

    if (argc < 2) {
        shell_print(sh, "Usage: ultrasonic temp <celsius>");
        return -EINVAL;
    }

    float temp = atof(argv[1]);
    ret = ultrasonic_set_temperature(dev, temp);
    if (ret < 0) {
        shell_error(sh, "Failed to set temperature: %d", ret);
        return ret;
    }

    shell_print(sh, "Temperature set to %.1f C", (double)temp);
    shell_print(sh, "Speed of sound adjusted accordingly");

    return 0;
}

static int cmd_ultrasonic_range(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_ultrasonic_dev();
    uint32_t min_cm, max_cm;
    uint32_t distance;
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "Ultrasonic device not available");
        return -ENODEV;
    }

    if (argc < 3) {
        shell_print(sh, "Usage: ultrasonic range <min_cm> <max_cm>");
        shell_print(sh, "Checks if object is within specified range");
        return -EINVAL;
    }

    min_cm = atoi(argv[1]);
    max_cm = atoi(argv[2]);

    ret = ultrasonic_get_distance_cm(dev, &distance);
    if (ret < 0) {
        shell_error(sh, "Measurement failed: %d", ret);
        return ret;
    }

    bool in_range = (distance >= min_cm && distance <= max_cm);

    shell_print(sh, "Distance: %u cm", distance);
    shell_print(sh, "Range [%u - %u cm]: %s",
                min_cm, max_cm, in_range ? "IN RANGE" : "OUT OF RANGE");

    return 0;
}

static int cmd_ultrasonic_info(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_ultrasonic_dev();

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "Ultrasonic device not available");
        return -ENODEV;
    }

    shell_print(sh, "Ultrasonic Sensor Info");
    shell_print(sh, "======================");
    shell_print(sh, "Minimum range: %u cm", ultrasonic_get_min_range(dev));
    shell_print(sh, "Maximum range: %u cm", ultrasonic_get_max_range(dev));
    shell_print(sh, "Trigger pulse: %u us", CONFIG_ULTRASONIC_TRIGGER_PULSE_US);
    shell_print(sh, "Echo timeout:  %u ms", CONFIG_ULTRASONIC_ECHO_TIMEOUT_MS);
    shell_print(sh, "Min interval:  %u ms", CONFIG_ULTRASONIC_MIN_INTERVAL_MS);
#ifdef CONFIG_ULTRASONIC_MEDIAN_FILTER
    shell_print(sh, "Median filter: %u samples", CONFIG_ULTRASONIC_MEDIAN_SAMPLES);
#else
    shell_print(sh, "Median filter: disabled");
#endif

    return 0;
}

static int cmd_ultrasonic_detect(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_ultrasonic_dev();
    uint32_t threshold = 30;
    uint32_t duration = 10;
    uint32_t distance;
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "Ultrasonic device not available");
        return -ENODEV;
    }

    if (argc > 1) {
        threshold = atoi(argv[1]);
    }
    if (argc > 2) {
        duration = atoi(argv[2]);
    }

    shell_print(sh, "Detecting objects closer than %u cm for %u seconds...",
                threshold, duration);

    uint32_t end_time = k_uptime_get_32() + (duration * 1000);
    int detections = 0;

    while (k_uptime_get_32() < end_time) {
        ret = ultrasonic_get_distance_cm(dev, &distance);
        if (ret == 0 && distance < threshold) {
            detections++;
            shell_print(sh, "DETECTED! Distance: %u cm", distance);
        }
        k_sleep(K_MSEC(100));
    }

    shell_print(sh, "Detection complete. Total detections: %d", detections);

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(ultrasonic_cmds,
    SHELL_CMD(measure, NULL, "Take single measurement", cmd_ultrasonic_measure),
    SHELL_CMD(filtered, NULL, "Filtered measurement: filtered [samples]",
              cmd_ultrasonic_filtered),
    SHELL_CMD(monitor, NULL, "Monitor: monitor [count] [interval_ms]",
              cmd_ultrasonic_monitor),
    SHELL_CMD(temp, NULL, "Set temperature: temp <celsius>", cmd_ultrasonic_temp),
    SHELL_CMD(range, NULL, "Check range: range <min_cm> <max_cm>",
              cmd_ultrasonic_range),
    SHELL_CMD(info, NULL, "Show sensor info", cmd_ultrasonic_info),
    SHELL_CMD(detect, NULL, "Detect objects: detect [threshold_cm] [duration_s]",
              cmd_ultrasonic_detect),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(ultrasonic, &ultrasonic_cmds, "Ultrasonic sensor commands", NULL);
