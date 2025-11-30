/**
 * @file mpu6050_shell.c
 * @brief Shell commands for MPU6050 driver
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/device.h>
#include <stdlib.h>

#include "mpu6050.h"

#define MPU6050_DEVICE DT_NODELABEL(mpu6050)

static const struct device *get_mpu6050_dev(void)
{
#if DT_NODE_EXISTS(MPU6050_DEVICE)
    return DEVICE_DT_GET(MPU6050_DEVICE);
#else
    return NULL;
#endif
}

static int cmd_mpu6050_read(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_mpu6050_dev();
    struct mpu6050_data data;
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "MPU6050 device not available");
        return -ENODEV;
    }

    ret = mpu6050_read(dev, &data);
    if (ret < 0) {
        shell_error(sh, "Read failed: %d", ret);
        return ret;
    }

    shell_print(sh, "MPU6050 Sensor Data");
    shell_print(sh, "===================");
    shell_print(sh, "Accelerometer (m/s^2):");
    shell_print(sh, "  X: %.3f", (double)data.accel_x);
    shell_print(sh, "  Y: %.3f", (double)data.accel_y);
    shell_print(sh, "  Z: %.3f", (double)data.accel_z);
    shell_print(sh, "");
    shell_print(sh, "Gyroscope (rad/s):");
    shell_print(sh, "  X: %.3f", (double)data.gyro_x);
    shell_print(sh, "  Y: %.3f", (double)data.gyro_y);
    shell_print(sh, "  Z: %.3f", (double)data.gyro_z);
    shell_print(sh, "");
    shell_print(sh, "Temperature: %.2f C", (double)data.temp);

    return 0;
}

static int cmd_mpu6050_raw(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_mpu6050_dev();
    struct mpu6050_raw_data data;
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "MPU6050 device not available");
        return -ENODEV;
    }

    ret = mpu6050_read_raw(dev, &data);
    if (ret < 0) {
        shell_error(sh, "Read failed: %d", ret);
        return ret;
    }

    shell_print(sh, "MPU6050 Raw Data");
    shell_print(sh, "================");
    shell_print(sh, "Accel: X=%d, Y=%d, Z=%d",
                data.accel_x, data.accel_y, data.accel_z);
    shell_print(sh, "Gyro:  X=%d, Y=%d, Z=%d",
                data.gyro_x, data.gyro_y, data.gyro_z);
    shell_print(sh, "Temp:  %d", data.temp);

    return 0;
}

static int cmd_mpu6050_whoami(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_mpu6050_dev();
    uint8_t id;
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "MPU6050 device not available");
        return -ENODEV;
    }

    ret = mpu6050_who_am_i(dev, &id);
    if (ret < 0) {
        shell_error(sh, "WHO_AM_I read failed: %d", ret);
        return ret;
    }

    shell_print(sh, "WHO_AM_I = 0x%02x (%s)",
                id, (id == 0x68) ? "valid MPU6050" : "unknown");

    return 0;
}

static int cmd_mpu6050_config(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_mpu6050_dev();
    struct mpu6050_config cfg;
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "MPU6050 device not available");
        return -ENODEV;
    }

    ret = mpu6050_config_get(dev, &cfg);
    if (ret < 0) {
        shell_error(sh, "Config read failed: %d", ret);
        return ret;
    }

    const char *accel_fs_str[] = {"+/-2g", "+/-4g", "+/-8g", "+/-16g"};
    const char *gyro_fs_str[] = {"+/-250dps", "+/-500dps", "+/-1000dps", "+/-2000dps"};

    shell_print(sh, "MPU6050 Configuration");
    shell_print(sh, "=====================");
    shell_print(sh, "Accel range:     %s", accel_fs_str[cfg.accel_fs]);
    shell_print(sh, "Gyro range:      %s", gyro_fs_str[cfg.gyro_fs]);
    shell_print(sh, "Sample rate div: %u", cfg.sample_rate_div);
    shell_print(sh, "DLPF config:     %u", cfg.dlpf_cfg);

    return 0;
}

static int cmd_mpu6050_set_accel(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_mpu6050_dev();
    int ret;
    int fs;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "MPU6050 device not available");
        return -ENODEV;
    }

    if (argc < 2) {
        shell_print(sh, "Usage: mpu6050 accel <range>");
        shell_print(sh, "  range: 0=2g, 1=4g, 2=8g, 3=16g");
        return -EINVAL;
    }

    fs = atoi(argv[1]);
    if (fs < 0 || fs > 3) {
        shell_error(sh, "Invalid range (0-3)");
        return -EINVAL;
    }

    ret = mpu6050_set_accel_fs(dev, fs);
    if (ret < 0) {
        shell_error(sh, "Set accel range failed: %d", ret);
        return ret;
    }

    const char *fs_str[] = {"+/-2g", "+/-4g", "+/-8g", "+/-16g"};
    shell_print(sh, "Accel range set to %s", fs_str[fs]);

    return 0;
}

static int cmd_mpu6050_set_gyro(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_mpu6050_dev();
    int ret;
    int fs;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "MPU6050 device not available");
        return -ENODEV;
    }

    if (argc < 2) {
        shell_print(sh, "Usage: mpu6050 gyro <range>");
        shell_print(sh, "  range: 0=250dps, 1=500dps, 2=1000dps, 3=2000dps");
        return -EINVAL;
    }

    fs = atoi(argv[1]);
    if (fs < 0 || fs > 3) {
        shell_error(sh, "Invalid range (0-3)");
        return -EINVAL;
    }

    ret = mpu6050_set_gyro_fs(dev, fs);
    if (ret < 0) {
        shell_error(sh, "Set gyro range failed: %d", ret);
        return ret;
    }

    const char *fs_str[] = {"+/-250dps", "+/-500dps", "+/-1000dps", "+/-2000dps"};
    shell_print(sh, "Gyro range set to %s", fs_str[fs]);

    return 0;
}

static int cmd_mpu6050_selftest(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_mpu6050_dev();
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "MPU6050 device not available");
        return -ENODEV;
    }

    shell_print(sh, "Running self-test...");

    ret = mpu6050_self_test(dev);
    if (ret < 0) {
        shell_error(sh, "Self-test FAILED: %d", ret);
        return ret;
    }

    shell_print(sh, "Self-test PASSED");

    return 0;
}

static int cmd_mpu6050_reset(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_mpu6050_dev();
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "MPU6050 device not available");
        return -ENODEV;
    }

    ret = mpu6050_reset(dev);
    if (ret < 0) {
        shell_error(sh, "Reset failed: %d", ret);
        return ret;
    }

    shell_print(sh, "Reset complete");

    return 0;
}

static int cmd_mpu6050_reg(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_mpu6050_dev();
    uint8_t reg, value;
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "MPU6050 device not available");
        return -ENODEV;
    }

    if (argc < 2) {
        shell_print(sh, "Usage: mpu6050 reg <addr> [value]");
        shell_print(sh, "  addr: Register address (hex)");
        shell_print(sh, "  value: Value to write (hex, optional)");
        return -EINVAL;
    }

    reg = (uint8_t)strtol(argv[1], NULL, 16);

    if (argc > 2) {
        /* Write */
        value = (uint8_t)strtol(argv[2], NULL, 16);
        ret = mpu6050_reg_write(dev, reg, value);
        if (ret < 0) {
            shell_error(sh, "Write failed: %d", ret);
            return ret;
        }
        shell_print(sh, "Wrote 0x%02x to reg 0x%02x", value, reg);
    } else {
        /* Read */
        ret = mpu6050_reg_read(dev, reg, &value);
        if (ret < 0) {
            shell_error(sh, "Read failed: %d", ret);
            return ret;
        }
        shell_print(sh, "Reg 0x%02x = 0x%02x", reg, value);
    }

    return 0;
}

static int cmd_mpu6050_monitor(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_mpu6050_dev();
    struct mpu6050_data data;
    int count = 10;
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "MPU6050 device not available");
        return -ENODEV;
    }

    if (argc > 1) {
        count = atoi(argv[1]);
    }

    shell_print(sh, "Monitoring %d samples...", count);
    shell_print(sh, "Accel X,Y,Z (m/s^2) | Gyro X,Y,Z (rad/s) | Temp (C)");
    shell_print(sh, "---------------------------------------------------");

    for (int i = 0; i < count; i++) {
        ret = mpu6050_read(dev, &data);
        if (ret < 0) {
            shell_error(sh, "Read failed: %d", ret);
            return ret;
        }

        shell_print(sh, "%.2f,%.2f,%.2f | %.2f,%.2f,%.2f | %.1f",
                    (double)data.accel_x, (double)data.accel_y, (double)data.accel_z,
                    (double)data.gyro_x, (double)data.gyro_y, (double)data.gyro_z,
                    (double)data.temp);

        k_sleep(K_MSEC(100));
    }

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(mpu6050_cmds,
    SHELL_CMD(read, NULL, "Read sensor data", cmd_mpu6050_read),
    SHELL_CMD(raw, NULL, "Read raw sensor data", cmd_mpu6050_raw),
    SHELL_CMD(whoami, NULL, "Read WHO_AM_I register", cmd_mpu6050_whoami),
    SHELL_CMD(config, NULL, "Show configuration", cmd_mpu6050_config),
    SHELL_CMD(accel, NULL, "Set accel range: accel <0-3>", cmd_mpu6050_set_accel),
    SHELL_CMD(gyro, NULL, "Set gyro range: gyro <0-3>", cmd_mpu6050_set_gyro),
    SHELL_CMD(selftest, NULL, "Run self-test", cmd_mpu6050_selftest),
    SHELL_CMD(reset, NULL, "Reset sensor", cmd_mpu6050_reset),
    SHELL_CMD(reg, NULL, "Register access: reg <addr> [value]", cmd_mpu6050_reg),
    SHELL_CMD(monitor, NULL, "Monitor data: monitor [count]", cmd_mpu6050_monitor),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(mpu6050, &mpu6050_cmds, "MPU6050 sensor commands", NULL);
