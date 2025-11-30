/**
 * @file main.c
 * @brief MPU6050 Example Application
 *
 * Demonstrates reading accelerometer and gyroscope data from MPU6050.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <math.h>

#include "mpu6050.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Get device from devicetree */
#if DT_NODE_EXISTS(DT_NODELABEL(mpu6050))
#define MPU6050_NODE DT_NODELABEL(mpu6050)
#else
#error "No mpu6050 node found in devicetree"
#endif

static void print_orientation(const struct mpu6050_data *data)
{
    /* Calculate pitch and roll from accelerometer data */
    float pitch = atan2f(data->accel_x,
                         sqrtf(data->accel_y * data->accel_y +
                               data->accel_z * data->accel_z)) * 180.0f / 3.14159f;

    float roll = atan2f(data->accel_y,
                        sqrtf(data->accel_x * data->accel_x +
                              data->accel_z * data->accel_z)) * 180.0f / 3.14159f;

    LOG_INF("Orientation: Pitch=%.1f, Roll=%.1f degrees",
            (double)pitch, (double)roll);
}

int main(void)
{
    const struct device *dev = DEVICE_DT_GET(MPU6050_NODE);
    struct mpu6050_data data;
    uint8_t id;
    int ret;

    LOG_INF("MPU6050 Example Application");
    LOG_INF("===========================");

    /* Check device is ready */
    if (!device_is_ready(dev)) {
        LOG_ERR("MPU6050 device not ready");
        return -ENODEV;
    }

    /* Read WHO_AM_I */
    ret = mpu6050_who_am_i(dev, &id);
    if (ret < 0) {
        LOG_ERR("Failed to read WHO_AM_I: %d", ret);
        return ret;
    }
    LOG_INF("WHO_AM_I: 0x%02x", id);

    /* Configure sensor */
    struct mpu6050_config cfg = {
        .accel_fs = MPU6050_ACCEL_FS_2G,
        .gyro_fs = MPU6050_GYRO_FS_250DPS,
        .sample_rate_div = 9,  /* 100 Hz sample rate */
        .dlpf_cfg = 6,         /* 5 Hz low-pass filter */
    };

    ret = mpu6050_configure(dev, &cfg);
    if (ret < 0) {
        LOG_ERR("Configuration failed: %d", ret);
        return ret;
    }
    LOG_INF("Sensor configured");

    LOG_INF("");
    LOG_INF("Reading sensor data...");
    LOG_INF("");

    /* Main loop */
    while (1) {
        ret = mpu6050_read(dev, &data);
        if (ret < 0) {
            LOG_ERR("Read failed: %d", ret);
        } else {
            LOG_INF("Accel: X=%.2f, Y=%.2f, Z=%.2f m/s^2",
                    (double)data.accel_x,
                    (double)data.accel_y,
                    (double)data.accel_z);

            LOG_INF("Gyro:  X=%.2f, Y=%.2f, Z=%.2f rad/s",
                    (double)data.gyro_x,
                    (double)data.gyro_y,
                    (double)data.gyro_z);

            LOG_INF("Temp:  %.1f C", (double)data.temp);

            print_orientation(&data);

            LOG_INF("");
        }

        k_sleep(K_MSEC(500));
    }

    return 0;
}
