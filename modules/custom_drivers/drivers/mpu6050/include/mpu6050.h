/**
 * @file mpu6050.h
 * @brief MPU6050 6-axis IMU Sensor Driver API
 *
 * This driver provides access to the InvenSense MPU6050 sensor:
 * - 3-axis accelerometer (+/- 2g, 4g, 8g, 16g)
 * - 3-axis gyroscope (+/- 250, 500, 1000, 2000 dps)
 * - Temperature sensor
 * - Interrupt-based data ready trigger
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MPU6050_H_
#define MPU6050_H_

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== Register Addresses ========== */

#define MPU6050_REG_SMPLRT_DIV       0x19
#define MPU6050_REG_CONFIG           0x1A
#define MPU6050_REG_GYRO_CONFIG      0x1B
#define MPU6050_REG_ACCEL_CONFIG     0x1C
#define MPU6050_REG_FIFO_EN          0x23
#define MPU6050_REG_INT_PIN_CFG      0x37
#define MPU6050_REG_INT_ENABLE       0x38
#define MPU6050_REG_INT_STATUS       0x3A
#define MPU6050_REG_ACCEL_XOUT_H     0x3B
#define MPU6050_REG_ACCEL_XOUT_L     0x3C
#define MPU6050_REG_ACCEL_YOUT_H     0x3D
#define MPU6050_REG_ACCEL_YOUT_L     0x3E
#define MPU6050_REG_ACCEL_ZOUT_H     0x3F
#define MPU6050_REG_ACCEL_ZOUT_L     0x40
#define MPU6050_REG_TEMP_OUT_H       0x41
#define MPU6050_REG_TEMP_OUT_L       0x42
#define MPU6050_REG_GYRO_XOUT_H      0x43
#define MPU6050_REG_GYRO_XOUT_L      0x44
#define MPU6050_REG_GYRO_YOUT_H      0x45
#define MPU6050_REG_GYRO_YOUT_L      0x46
#define MPU6050_REG_GYRO_ZOUT_H      0x47
#define MPU6050_REG_GYRO_ZOUT_L      0x48
#define MPU6050_REG_USER_CTRL        0x6A
#define MPU6050_REG_PWR_MGMT_1       0x6B
#define MPU6050_REG_PWR_MGMT_2       0x6C
#define MPU6050_REG_WHO_AM_I         0x75

/* WHO_AM_I expected value */
#define MPU6050_WHO_AM_I_VALUE       0x68

/* ========== Types ========== */

/**
 * @brief Accelerometer full-scale range
 */
enum mpu6050_accel_fs {
    MPU6050_ACCEL_FS_2G  = 0,  /**< +/- 2g */
    MPU6050_ACCEL_FS_4G  = 1,  /**< +/- 4g */
    MPU6050_ACCEL_FS_8G  = 2,  /**< +/- 8g */
    MPU6050_ACCEL_FS_16G = 3,  /**< +/- 16g */
};

/**
 * @brief Gyroscope full-scale range
 */
enum mpu6050_gyro_fs {
    MPU6050_GYRO_FS_250DPS  = 0,  /**< +/- 250 dps */
    MPU6050_GYRO_FS_500DPS  = 1,  /**< +/- 500 dps */
    MPU6050_GYRO_FS_1000DPS = 2,  /**< +/- 1000 dps */
    MPU6050_GYRO_FS_2000DPS = 3,  /**< +/- 2000 dps */
};

/**
 * @brief Raw sensor data (16-bit signed)
 */
struct mpu6050_raw_data {
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
    int16_t temp;
    int16_t gyro_x;
    int16_t gyro_y;
    int16_t gyro_z;
};

/**
 * @brief Scaled sensor data (floating point)
 */
struct mpu6050_data {
    float accel_x;    /**< Acceleration X in m/s^2 */
    float accel_y;    /**< Acceleration Y in m/s^2 */
    float accel_z;    /**< Acceleration Z in m/s^2 */
    float temp;       /**< Temperature in Celsius */
    float gyro_x;     /**< Angular velocity X in rad/s */
    float gyro_y;     /**< Angular velocity Y in rad/s */
    float gyro_z;     /**< Angular velocity Z in rad/s */
};

/**
 * @brief Sensor configuration
 */
struct mpu6050_config {
    enum mpu6050_accel_fs accel_fs;  /**< Accelerometer range */
    enum mpu6050_gyro_fs gyro_fs;    /**< Gyroscope range */
    uint8_t sample_rate_div;         /**< Sample rate divider */
    uint8_t dlpf_cfg;                /**< DLPF configuration */
};

/**
 * @brief Trigger callback type
 */
typedef void (*mpu6050_trigger_callback_t)(const struct device *dev,
                                           void *user_data);

/* ========== API Functions ========== */

/**
 * @brief Configure the sensor
 *
 * @param dev Device instance
 * @param cfg Configuration parameters
 * @return 0 on success, negative errno on failure
 */
int mpu6050_configure(const struct device *dev,
                      const struct mpu6050_config *cfg);

/**
 * @brief Get current configuration
 *
 * @param dev Device instance
 * @param cfg Pointer to store configuration
 * @return 0 on success, negative errno on failure
 */
int mpu6050_config_get(const struct device *dev,
                       struct mpu6050_config *cfg);

/**
 * @brief Read raw sensor data
 *
 * @param dev Device instance
 * @param data Pointer to store raw data
 * @return 0 on success, negative errno on failure
 */
int mpu6050_read_raw(const struct device *dev,
                     struct mpu6050_raw_data *data);

/**
 * @brief Read scaled sensor data
 *
 * @param dev Device instance
 * @param data Pointer to store scaled data
 * @return 0 on success, negative errno on failure
 */
int mpu6050_read(const struct device *dev,
                 struct mpu6050_data *data);

/**
 * @brief Read only accelerometer (scaled)
 *
 * @param dev Device instance
 * @param x Pointer to X acceleration (m/s^2)
 * @param y Pointer to Y acceleration (m/s^2)
 * @param z Pointer to Z acceleration (m/s^2)
 * @return 0 on success, negative errno on failure
 */
int mpu6050_read_accel(const struct device *dev,
                       float *x, float *y, float *z);

/**
 * @brief Read only gyroscope (scaled)
 *
 * @param dev Device instance
 * @param x Pointer to X angular velocity (rad/s)
 * @param y Pointer to Y angular velocity (rad/s)
 * @param z Pointer to Z angular velocity (rad/s)
 * @return 0 on success, negative errno on failure
 */
int mpu6050_read_gyro(const struct device *dev,
                      float *x, float *y, float *z);

/**
 * @brief Read temperature
 *
 * @param dev Device instance
 * @param temp Pointer to temperature (Celsius)
 * @return 0 on success, negative errno on failure
 */
int mpu6050_read_temp(const struct device *dev, float *temp);

/**
 * @brief Set accelerometer full-scale range
 *
 * @param dev Device instance
 * @param fs Full-scale range
 * @return 0 on success, negative errno on failure
 */
int mpu6050_set_accel_fs(const struct device *dev,
                         enum mpu6050_accel_fs fs);

/**
 * @brief Set gyroscope full-scale range
 *
 * @param dev Device instance
 * @param fs Full-scale range
 * @return 0 on success, negative errno on failure
 */
int mpu6050_set_gyro_fs(const struct device *dev,
                        enum mpu6050_gyro_fs fs);

/**
 * @brief Set sample rate divider
 *
 * @param dev Device instance
 * @param div Divider value (0-255)
 * @return 0 on success, negative errno on failure
 */
int mpu6050_set_sample_rate_div(const struct device *dev, uint8_t div);

/**
 * @brief Set DLPF configuration
 *
 * @param dev Device instance
 * @param cfg DLPF config (0-7)
 * @return 0 on success, negative errno on failure
 */
int mpu6050_set_dlpf(const struct device *dev, uint8_t cfg);

/**
 * @brief Enable/disable sleep mode
 *
 * @param dev Device instance
 * @param enable true to enable sleep, false to disable
 * @return 0 on success, negative errno on failure
 */
int mpu6050_sleep(const struct device *dev, bool enable);

/**
 * @brief Reset the sensor
 *
 * @param dev Device instance
 * @return 0 on success, negative errno on failure
 */
int mpu6050_reset(const struct device *dev);

/**
 * @brief Self-test the sensor
 *
 * @param dev Device instance
 * @return 0 if test passed, negative errno on failure
 */
int mpu6050_self_test(const struct device *dev);

/* ========== Trigger API ========== */

#ifdef CONFIG_MPU6050_TRIGGER
/**
 * @brief Set data ready trigger callback
 *
 * @param dev Device instance
 * @param callback Callback function (NULL to disable)
 * @param user_data User data passed to callback
 * @return 0 on success, negative errno on failure
 */
int mpu6050_trigger_set(const struct device *dev,
                        mpu6050_trigger_callback_t callback,
                        void *user_data);
#endif

/* ========== Register Access (for debugging) ========== */

/**
 * @brief Read single register
 *
 * @param dev Device instance
 * @param reg Register address
 * @param value Pointer to store value
 * @return 0 on success, negative errno on failure
 */
int mpu6050_reg_read(const struct device *dev, uint8_t reg, uint8_t *value);

/**
 * @brief Write single register
 *
 * @param dev Device instance
 * @param reg Register address
 * @param value Value to write
 * @return 0 on success, negative errno on failure
 */
int mpu6050_reg_write(const struct device *dev, uint8_t reg, uint8_t value);

/**
 * @brief Read WHO_AM_I register
 *
 * @param dev Device instance
 * @param id Pointer to store device ID
 * @return 0 on success, negative errno on failure
 */
int mpu6050_who_am_i(const struct device *dev, uint8_t *id);

#ifdef __cplusplus
}
#endif

#endif /* MPU6050_H_ */
