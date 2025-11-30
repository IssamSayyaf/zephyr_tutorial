/**
 * @file mpu6050.c
 * @brief MPU6050 6-axis IMU Sensor Driver Implementation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT invensense_mpu6050_custom

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <math.h>

#include "mpu6050.h"

LOG_MODULE_REGISTER(mpu6050, CONFIG_MPU6050_CUSTOM_LOG_LEVEL);

/* Conversion constants */
#define GRAVITY_MS2       9.80665f
#define DEG_TO_RAD        0.017453292519943f

/* Sensitivity values */
static const float accel_sensitivity[] = {
    16384.0f,  /* 2g */
    8192.0f,   /* 4g */
    4096.0f,   /* 8g */
    2048.0f,   /* 16g */
};

static const float gyro_sensitivity[] = {
    131.0f,    /* 250 dps */
    65.5f,     /* 500 dps */
    32.8f,     /* 1000 dps */
    16.4f,     /* 2000 dps */
};

/**
 * @brief Driver runtime data
 */
struct mpu6050_drv_data {
    struct mpu6050_config config;

#ifdef CONFIG_MPU6050_TRIGGER
    const struct device *dev;
    struct gpio_callback gpio_cb;
    mpu6050_trigger_callback_t callback;
    void *user_data;

#ifdef CONFIG_MPU6050_TRIGGER_OWN_THREAD
    K_KERNEL_STACK_MEMBER(thread_stack, CONFIG_MPU6050_THREAD_STACK_SIZE);
    struct k_thread thread;
    struct k_sem trig_sem;
#elif defined(CONFIG_MPU6050_TRIGGER_GLOBAL_THREAD)
    struct k_work work;
#endif
#endif /* CONFIG_MPU6050_TRIGGER */

    struct k_mutex lock;
    bool initialized;
};

/**
 * @brief Driver configuration
 */
struct mpu6050_drv_cfg {
    struct i2c_dt_spec i2c;

#ifdef CONFIG_MPU6050_TRIGGER
    struct gpio_dt_spec int_gpio;
#endif

    enum mpu6050_accel_fs accel_fs;
    enum mpu6050_gyro_fs gyro_fs;
    uint8_t sample_rate_div;
    uint8_t dlpf_cfg;
};

/* ========== Register Access ========== */

int mpu6050_reg_read(const struct device *dev, uint8_t reg, uint8_t *value)
{
    const struct mpu6050_drv_cfg *cfg = dev->config;

    return i2c_reg_read_byte_dt(&cfg->i2c, reg, value);
}

int mpu6050_reg_write(const struct device *dev, uint8_t reg, uint8_t value)
{
    const struct mpu6050_drv_cfg *cfg = dev->config;

    return i2c_reg_write_byte_dt(&cfg->i2c, reg, value);
}

static int mpu6050_reg_read_burst(const struct device *dev, uint8_t reg,
                                  uint8_t *buf, size_t len)
{
    const struct mpu6050_drv_cfg *cfg = dev->config;

    return i2c_burst_read_dt(&cfg->i2c, reg, buf, len);
}

int mpu6050_who_am_i(const struct device *dev, uint8_t *id)
{
    return mpu6050_reg_read(dev, MPU6050_REG_WHO_AM_I, id);
}

/* ========== Configuration ========== */

int mpu6050_configure(const struct device *dev,
                      const struct mpu6050_config *cfg)
{
    struct mpu6050_drv_data *data = dev->data;
    int ret;

    if (cfg == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&data->lock, K_FOREVER);

    /* Set accelerometer range */
    ret = mpu6050_reg_write(dev, MPU6050_REG_ACCEL_CONFIG,
                            cfg->accel_fs << 3);
    if (ret < 0) {
        goto unlock;
    }

    /* Set gyroscope range */
    ret = mpu6050_reg_write(dev, MPU6050_REG_GYRO_CONFIG,
                            cfg->gyro_fs << 3);
    if (ret < 0) {
        goto unlock;
    }

    /* Set sample rate divider */
    ret = mpu6050_reg_write(dev, MPU6050_REG_SMPLRT_DIV,
                            cfg->sample_rate_div);
    if (ret < 0) {
        goto unlock;
    }

    /* Set DLPF configuration */
    ret = mpu6050_reg_write(dev, MPU6050_REG_CONFIG,
                            cfg->dlpf_cfg);
    if (ret < 0) {
        goto unlock;
    }

    data->config = *cfg;

unlock:
    k_mutex_unlock(&data->lock);
    return ret;
}

int mpu6050_config_get(const struct device *dev,
                       struct mpu6050_config *cfg)
{
    struct mpu6050_drv_data *data = dev->data;

    if (cfg == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&data->lock, K_FOREVER);
    *cfg = data->config;
    k_mutex_unlock(&data->lock);

    return 0;
}

int mpu6050_set_accel_fs(const struct device *dev,
                         enum mpu6050_accel_fs fs)
{
    struct mpu6050_drv_data *data = dev->data;
    int ret;

    k_mutex_lock(&data->lock, K_FOREVER);
    ret = mpu6050_reg_write(dev, MPU6050_REG_ACCEL_CONFIG, fs << 3);
    if (ret == 0) {
        data->config.accel_fs = fs;
    }
    k_mutex_unlock(&data->lock);

    return ret;
}

int mpu6050_set_gyro_fs(const struct device *dev,
                        enum mpu6050_gyro_fs fs)
{
    struct mpu6050_drv_data *data = dev->data;
    int ret;

    k_mutex_lock(&data->lock, K_FOREVER);
    ret = mpu6050_reg_write(dev, MPU6050_REG_GYRO_CONFIG, fs << 3);
    if (ret == 0) {
        data->config.gyro_fs = fs;
    }
    k_mutex_unlock(&data->lock);

    return ret;
}

int mpu6050_set_sample_rate_div(const struct device *dev, uint8_t div)
{
    struct mpu6050_drv_data *data = dev->data;
    int ret;

    k_mutex_lock(&data->lock, K_FOREVER);
    ret = mpu6050_reg_write(dev, MPU6050_REG_SMPLRT_DIV, div);
    if (ret == 0) {
        data->config.sample_rate_div = div;
    }
    k_mutex_unlock(&data->lock);

    return ret;
}

int mpu6050_set_dlpf(const struct device *dev, uint8_t cfg)
{
    struct mpu6050_drv_data *data = dev->data;
    int ret;

    if (cfg > 7) {
        return -EINVAL;
    }

    k_mutex_lock(&data->lock, K_FOREVER);
    ret = mpu6050_reg_write(dev, MPU6050_REG_CONFIG, cfg);
    if (ret == 0) {
        data->config.dlpf_cfg = cfg;
    }
    k_mutex_unlock(&data->lock);

    return ret;
}

/* ========== Power Management ========== */

int mpu6050_sleep(const struct device *dev, bool enable)
{
    uint8_t val;
    int ret;

    ret = mpu6050_reg_read(dev, MPU6050_REG_PWR_MGMT_1, &val);
    if (ret < 0) {
        return ret;
    }

    if (enable) {
        val |= BIT(6);  /* Set SLEEP bit */
    } else {
        val &= ~BIT(6); /* Clear SLEEP bit */
    }

    return mpu6050_reg_write(dev, MPU6050_REG_PWR_MGMT_1, val);
}

int mpu6050_reset(const struct device *dev)
{
    int ret;

    /* Set DEVICE_RESET bit */
    ret = mpu6050_reg_write(dev, MPU6050_REG_PWR_MGMT_1, BIT(7));
    if (ret < 0) {
        return ret;
    }

    /* Wait for reset to complete */
    k_sleep(K_MSEC(100));

    /* Wake up from sleep (reset sets sleep mode) */
    return mpu6050_sleep(dev, false);
}

/* ========== Data Reading ========== */

int mpu6050_read_raw(const struct device *dev,
                     struct mpu6050_raw_data *data)
{
    struct mpu6050_drv_data *drv_data = dev->data;
    uint8_t buf[14];
    int ret;

    if (data == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&drv_data->lock, K_FOREVER);

    /* Read all sensor data in one burst (14 bytes starting at ACCEL_XOUT_H) */
    ret = mpu6050_reg_read_burst(dev, MPU6050_REG_ACCEL_XOUT_H, buf, 14);

    k_mutex_unlock(&drv_data->lock);

    if (ret < 0) {
        return ret;
    }

    /* Parse data (big-endian) */
    data->accel_x = (int16_t)sys_get_be16(&buf[0]);
    data->accel_y = (int16_t)sys_get_be16(&buf[2]);
    data->accel_z = (int16_t)sys_get_be16(&buf[4]);
    data->temp    = (int16_t)sys_get_be16(&buf[6]);
    data->gyro_x  = (int16_t)sys_get_be16(&buf[8]);
    data->gyro_y  = (int16_t)sys_get_be16(&buf[10]);
    data->gyro_z  = (int16_t)sys_get_be16(&buf[12]);

    return 0;
}

int mpu6050_read(const struct device *dev, struct mpu6050_data *data)
{
    struct mpu6050_drv_data *drv_data = dev->data;
    struct mpu6050_raw_data raw;
    float accel_sens, gyro_sens;
    int ret;

    if (data == NULL) {
        return -EINVAL;
    }

    ret = mpu6050_read_raw(dev, &raw);
    if (ret < 0) {
        return ret;
    }

    /* Get sensitivity values */
    accel_sens = accel_sensitivity[drv_data->config.accel_fs];
    gyro_sens = gyro_sensitivity[drv_data->config.gyro_fs];

    /* Convert accelerometer to m/s^2 */
    data->accel_x = (raw.accel_x / accel_sens) * GRAVITY_MS2;
    data->accel_y = (raw.accel_y / accel_sens) * GRAVITY_MS2;
    data->accel_z = (raw.accel_z / accel_sens) * GRAVITY_MS2;

    /* Convert gyroscope to rad/s */
    data->gyro_x = (raw.gyro_x / gyro_sens) * DEG_TO_RAD;
    data->gyro_y = (raw.gyro_y / gyro_sens) * DEG_TO_RAD;
    data->gyro_z = (raw.gyro_z / gyro_sens) * DEG_TO_RAD;

    /* Convert temperature */
    data->temp = (raw.temp / 340.0f) + 36.53f;

    return 0;
}

int mpu6050_read_accel(const struct device *dev,
                       float *x, float *y, float *z)
{
    struct mpu6050_data data;
    int ret;

    ret = mpu6050_read(dev, &data);
    if (ret < 0) {
        return ret;
    }

    if (x) *x = data.accel_x;
    if (y) *y = data.accel_y;
    if (z) *z = data.accel_z;

    return 0;
}

int mpu6050_read_gyro(const struct device *dev,
                      float *x, float *y, float *z)
{
    struct mpu6050_data data;
    int ret;

    ret = mpu6050_read(dev, &data);
    if (ret < 0) {
        return ret;
    }

    if (x) *x = data.gyro_x;
    if (y) *y = data.gyro_y;
    if (z) *z = data.gyro_z;

    return 0;
}

int mpu6050_read_temp(const struct device *dev, float *temp)
{
    struct mpu6050_data data;
    int ret;

    if (temp == NULL) {
        return -EINVAL;
    }

    ret = mpu6050_read(dev, &data);
    if (ret < 0) {
        return ret;
    }

    *temp = data.temp;

    return 0;
}

/* ========== Self Test ========== */

int mpu6050_self_test(const struct device *dev)
{
    struct mpu6050_raw_data normal, self_test;
    int ret;
    uint8_t orig_accel_cfg, orig_gyro_cfg;

    LOG_INF("Starting self-test...");

    /* Save current configuration */
    ret = mpu6050_reg_read(dev, MPU6050_REG_ACCEL_CONFIG, &orig_accel_cfg);
    if (ret < 0) return ret;

    ret = mpu6050_reg_read(dev, MPU6050_REG_GYRO_CONFIG, &orig_gyro_cfg);
    if (ret < 0) return ret;

    /* Read normal values */
    ret = mpu6050_read_raw(dev, &normal);
    if (ret < 0) return ret;

    /* Enable self-test (set XA_ST, YA_ST, ZA_ST bits for accel, XG_ST, YG_ST, ZG_ST for gyro) */
    ret = mpu6050_reg_write(dev, MPU6050_REG_ACCEL_CONFIG, orig_accel_cfg | 0xE0);
    if (ret < 0) return ret;

    ret = mpu6050_reg_write(dev, MPU6050_REG_GYRO_CONFIG, orig_gyro_cfg | 0xE0);
    if (ret < 0) return ret;

    /* Wait for self-test to stabilize */
    k_sleep(K_MSEC(250));

    /* Read self-test values */
    ret = mpu6050_read_raw(dev, &self_test);
    if (ret < 0) return ret;

    /* Restore original configuration */
    ret = mpu6050_reg_write(dev, MPU6050_REG_ACCEL_CONFIG, orig_accel_cfg);
    if (ret < 0) return ret;

    ret = mpu6050_reg_write(dev, MPU6050_REG_GYRO_CONFIG, orig_gyro_cfg);
    if (ret < 0) return ret;

    /* Calculate self-test response */
    int16_t str_accel_x = self_test.accel_x - normal.accel_x;
    int16_t str_accel_y = self_test.accel_y - normal.accel_y;
    int16_t str_accel_z = self_test.accel_z - normal.accel_z;
    int16_t str_gyro_x = self_test.gyro_x - normal.gyro_x;
    int16_t str_gyro_y = self_test.gyro_y - normal.gyro_y;
    int16_t str_gyro_z = self_test.gyro_z - normal.gyro_z;

    LOG_INF("Self-test response:");
    LOG_INF("  Accel: X=%d, Y=%d, Z=%d", str_accel_x, str_accel_y, str_accel_z);
    LOG_INF("  Gyro:  X=%d, Y=%d, Z=%d", str_gyro_x, str_gyro_y, str_gyro_z);

    /* Simple pass/fail check - self-test response should be significant */
    if (abs(str_accel_x) < 100 || abs(str_accel_y) < 100 || abs(str_accel_z) < 100) {
        LOG_ERR("Accelerometer self-test failed");
        return -EIO;
    }

    if (abs(str_gyro_x) < 100 || abs(str_gyro_y) < 100 || abs(str_gyro_z) < 100) {
        LOG_ERR("Gyroscope self-test failed");
        return -EIO;
    }

    LOG_INF("Self-test PASSED");
    return 0;
}

/* ========== Initialization ========== */

static int mpu6050_init(const struct device *dev)
{
    const struct mpu6050_drv_cfg *cfg = dev->config;
    struct mpu6050_drv_data *data = dev->data;
    uint8_t id;
    int ret;

    LOG_DBG("Initializing %s", dev->name);

    /* Check I2C bus */
    if (!i2c_is_ready_dt(&cfg->i2c)) {
        LOG_ERR("I2C bus not ready");
        return -ENODEV;
    }

    k_mutex_init(&data->lock);

    /* Verify WHO_AM_I */
    ret = mpu6050_who_am_i(dev, &id);
    if (ret < 0) {
        LOG_ERR("Failed to read WHO_AM_I: %d", ret);
        return ret;
    }

    if (id != MPU6050_WHO_AM_I_VALUE) {
        LOG_ERR("Invalid WHO_AM_I: 0x%02x (expected 0x%02x)",
                id, MPU6050_WHO_AM_I_VALUE);
        return -ENODEV;
    }

    LOG_DBG("WHO_AM_I = 0x%02x", id);

    /* Reset device */
    ret = mpu6050_reset(dev);
    if (ret < 0) {
        LOG_ERR("Reset failed: %d", ret);
        return ret;
    }

    /* Configure sensor */
    struct mpu6050_config init_cfg = {
        .accel_fs = cfg->accel_fs,
        .gyro_fs = cfg->gyro_fs,
        .sample_rate_div = cfg->sample_rate_div,
        .dlpf_cfg = cfg->dlpf_cfg,
    };

    ret = mpu6050_configure(dev, &init_cfg);
    if (ret < 0) {
        LOG_ERR("Configuration failed: %d", ret);
        return ret;
    }

#ifdef CONFIG_MPU6050_TRIGGER
    /* Initialize trigger if configured */
    extern int mpu6050_trigger_init(const struct device *dev);
    ret = mpu6050_trigger_init(dev);
    if (ret < 0) {
        LOG_ERR("Trigger init failed: %d", ret);
        return ret;
    }
#endif

    data->initialized = true;

    LOG_INF("Initialized (I2C: %s@0x%02x)",
            cfg->i2c.bus->name, cfg->i2c.addr);

    return 0;
}

/* ========== Device Instantiation ========== */

#if defined(CONFIG_MPU6050_ACCEL_FS_2G)
#define MPU6050_ACCEL_FS_DEFAULT MPU6050_ACCEL_FS_2G
#elif defined(CONFIG_MPU6050_ACCEL_FS_4G)
#define MPU6050_ACCEL_FS_DEFAULT MPU6050_ACCEL_FS_4G
#elif defined(CONFIG_MPU6050_ACCEL_FS_8G)
#define MPU6050_ACCEL_FS_DEFAULT MPU6050_ACCEL_FS_8G
#else
#define MPU6050_ACCEL_FS_DEFAULT MPU6050_ACCEL_FS_16G
#endif

#if defined(CONFIG_MPU6050_GYRO_FS_250DPS)
#define MPU6050_GYRO_FS_DEFAULT MPU6050_GYRO_FS_250DPS
#elif defined(CONFIG_MPU6050_GYRO_FS_500DPS)
#define MPU6050_GYRO_FS_DEFAULT MPU6050_GYRO_FS_500DPS
#elif defined(CONFIG_MPU6050_GYRO_FS_1000DPS)
#define MPU6050_GYRO_FS_DEFAULT MPU6050_GYRO_FS_1000DPS
#else
#define MPU6050_GYRO_FS_DEFAULT MPU6050_GYRO_FS_2000DPS
#endif

#ifdef CONFIG_MPU6050_TRIGGER
#define MPU6050_INT_GPIO(inst) .int_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, int_gpios, {0}),
#else
#define MPU6050_INT_GPIO(inst)
#endif

#define MPU6050_INIT(inst)                                                  \
    static struct mpu6050_drv_data mpu6050_data_##inst;                     \
                                                                            \
    static const struct mpu6050_drv_cfg mpu6050_cfg_##inst = {              \
        .i2c = I2C_DT_SPEC_INST_GET(inst),                                 \
        MPU6050_INT_GPIO(inst)                                              \
        .accel_fs = DT_INST_PROP_OR(inst, accel_fs, MPU6050_ACCEL_FS_DEFAULT), \
        .gyro_fs = DT_INST_PROP_OR(inst, gyro_fs, MPU6050_GYRO_FS_DEFAULT), \
        .sample_rate_div = DT_INST_PROP_OR(inst, sample_rate_div,          \
                                           CONFIG_MPU6050_SAMPLE_RATE_DIV), \
        .dlpf_cfg = DT_INST_PROP_OR(inst, dlpf_cfg, CONFIG_MPU6050_DLPF_CFG), \
    };                                                                      \
                                                                            \
    DEVICE_DT_INST_DEFINE(inst,                                            \
                          mpu6050_init,                                     \
                          NULL,                                             \
                          &mpu6050_data_##inst,                             \
                          &mpu6050_cfg_##inst,                              \
                          POST_KERNEL,                                      \
                          CONFIG_MPU6050_CUSTOM_INIT_PRIORITY,              \
                          NULL);

DT_INST_FOREACH_STATUS_OKAY(MPU6050_INIT)
