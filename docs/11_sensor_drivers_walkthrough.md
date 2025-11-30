# Sensor Drivers Walkthrough - Complete Code Explanation

This document explains three sensor drivers in detail: MPU6050 (I2C IMU), uBlox NEO-M8N (UART GPS), and Ultrasonic (GPIO timing). Each demonstrates different patterns and techniques for Zephyr driver development.

## Table of Contents

1. [MPU6050 I2C IMU Driver](#mpu6050-i2c-imu-driver)
2. [uBlox NEO-M8N GPS Driver](#ublox-neo-m8n-gps-driver)
3. [Ultrasonic Distance Sensor Driver](#ultrasonic-distance-sensor-driver)
4. [Comparison and Patterns](#comparison-and-patterns)

---

## MPU6050 I2C IMU Driver

### Device Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                       MPU6050 IMU                                │
│                                                                  │
│   ┌─────────────────────────────────────────────────────┐       │
│   │                    MPU6050                          │       │
│   │                                                     │       │
│   │   ┌────────────┐    ┌────────────┐                 │       │
│   │   │  3-Axis    │    │  3-Axis    │                 │       │
│   │   │  Gyroscope │    │  Accel     │                 │       │
│   │   └────────────┘    └────────────┘                 │       │
│   │                                                     │       │
│   │   ┌────────────┐    ┌────────────┐                 │       │
│   │   │   DMP      │    │   Temp     │                 │       │
│   │   │  (Motion)  │    │   Sensor   │                 │       │
│   │   └────────────┘    └────────────┘                 │       │
│   │                                                     │       │
│   │   Registers: 0x00 - 0x75 (117 registers)           │       │
│   │   I2C Address: 0x68 (AD0=0) or 0x69 (AD0=1)        │       │
│   │   Interrupt: Data ready, motion detect, FIFO      │       │
│   │                                                     │       │
│   └─────────────────────────────────────────────────────┘       │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### Register Map (Key Registers)

```c
/**
 * MPU6050 Register Addresses
 *
 * WHY DEFINE: Self-documenting code, single point of change
 *
 * The MPU6050 has 117 registers. Here are the essential ones:
 */

/* Configuration registers */
#define MPU6050_REG_SMPLRT_DIV    0x19  /* Sample rate = Gyro rate / (1 + SMPLRT_DIV) */
#define MPU6050_REG_CONFIG        0x1A  /* DLPF (digital low-pass filter) config */
#define MPU6050_REG_GYRO_CONFIG   0x1B  /* Gyroscope full-scale range */
#define MPU6050_REG_ACCEL_CONFIG  0x1C  /* Accelerometer full-scale range */

/* Interrupt registers */
#define MPU6050_REG_INT_PIN_CFG   0x37  /* Interrupt pin config */
#define MPU6050_REG_INT_ENABLE    0x38  /* Interrupt enable */
#define MPU6050_REG_INT_STATUS    0x3A  /* Interrupt status (read to clear) */

/* Data registers - 6 bytes each for 3 axes (high + low byte) */
#define MPU6050_REG_ACCEL_XOUT_H  0x3B  /* Accel X high byte */
#define MPU6050_REG_ACCEL_XOUT_L  0x3C  /* Accel X low byte */
#define MPU6050_REG_ACCEL_YOUT_H  0x3D  /* ... */
/* ... continues for all axes ... */
#define MPU6050_REG_TEMP_OUT_H    0x41  /* Temperature high byte */
#define MPU6050_REG_GYRO_XOUT_H   0x43  /* Gyro X high byte */

/* Power management */
#define MPU6050_REG_PWR_MGMT_1    0x6B  /* Power management 1 */
#define MPU6050_REG_PWR_MGMT_2    0x6C  /* Power management 2 */

/* Device ID */
#define MPU6050_REG_WHO_AM_I      0x75  /* Should return 0x68 */

/**
 * Register Value Definitions
 */
#define MPU6050_WHO_AM_I_VAL      0x68  /* Expected WHO_AM_I response */
#define MPU6050_PWR1_SLEEP_BIT    0x40  /* Sleep mode bit */
#define MPU6050_PWR1_RESET_BIT    0x80  /* Device reset bit */
```

### Low-Level I2C Access

```c
/**
 * Read Single Register
 *
 * HOW I2C REGISTER READ WORKS:
 * 1. Master sends START
 * 2. Master sends slave address + WRITE bit
 * 3. Slave ACKs
 * 4. Master sends register address
 * 5. Slave ACKs
 * 6. Master sends REPEATED START
 * 7. Master sends slave address + READ bit
 * 8. Slave ACKs
 * 9. Slave sends data byte
 * 10. Master sends NAK (no more data wanted)
 * 11. Master sends STOP
 *
 * Zephyr abstracts this into one function call.
 */
static int mpu6050_reg_read(const struct device *dev, uint8_t reg, uint8_t *val)
{
    const struct mpu6050_config *cfg = dev->config;

    /**
     * i2c_reg_read_byte() performs the full I2C sequence:
     *
     * @param bus   I2C controller device
     * @param addr  Slave address (0x68 or 0x69 for MPU6050)
     * @param reg   Register address to read
     * @param val   Where to store the byte
     *
     * Returns 0 on success, negative errno on failure.
     */
    return i2c_reg_read_byte(cfg->i2c_bus, cfg->i2c_addr, reg, val);
}

/**
 * Burst Read Multiple Registers
 *
 * WHY BURST READ: More efficient than multiple single reads.
 * One I2C transaction instead of many.
 *
 * MPU6050 auto-increments register address, so reading from
 * 0x3B returns bytes from 0x3B, 0x3C, 0x3D, ...
 */
static int mpu6050_reg_burst_read(const struct device *dev,
                                   uint8_t start_reg,
                                   uint8_t *buf,
                                   size_t len)
{
    const struct mpu6050_config *cfg = dev->config;

    /**
     * i2c_burst_read():
     *
     * 1. Sends register address
     * 2. Reads 'len' consecutive bytes
     *
     * Perfect for reading sensor data:
     * - 6 bytes accel (X_H, X_L, Y_H, Y_L, Z_H, Z_L)
     * - 2 bytes temp (H, L)
     * - 6 bytes gyro (X_H, X_L, Y_H, Y_L, Z_H, Z_L)
     * Total: 14 bytes in one read!
     */
    return i2c_burst_read(cfg->i2c_bus, cfg->i2c_addr, start_reg, buf, len);
}

/**
 * Write Single Register
 */
static int mpu6050_reg_write(const struct device *dev, uint8_t reg, uint8_t val)
{
    const struct mpu6050_config *cfg = dev->config;

    return i2c_reg_write_byte(cfg->i2c_bus, cfg->i2c_addr, reg, val);
}
```

### Data Conversion

```c
/**
 * Reading Raw Sensor Data
 *
 * WHY RAW DATA: Sometimes needed for custom processing.
 * Also useful for debugging (see actual sensor values).
 */
int mpu6050_get_raw_data(const struct device *dev,
                         struct mpu6050_raw_data *data)
{
    uint8_t buf[14];  /* 6 accel + 2 temp + 6 gyro */
    int ret;

    /**
     * Read all sensor data in one burst
     *
     * Starting from ACCEL_XOUT_H (0x3B), read 14 consecutive bytes.
     * This is MUCH faster than 14 separate reads!
     */
    ret = mpu6050_reg_burst_read(dev, MPU6050_REG_ACCEL_XOUT_H, buf, 14);
    if (ret < 0) {
        LOG_ERR("Failed to read sensor data: %d", ret);
        return ret;
    }

    /**
     * Parse buffer into struct
     *
     * Data is in big-endian format (high byte first).
     * Each axis is 16-bit signed integer.
     *
     * WHY CAST TO INT16_T: Raw values are signed!
     * Range depends on full-scale setting:
     *   ±2g  accel: 1 LSB = 0.000061 g (16384 LSB/g)
     *   ±4g  accel: 1 LSB = 0.000122 g (8192 LSB/g)
     *   etc.
     */
    data->accel_x = (int16_t)((buf[0] << 8) | buf[1]);
    data->accel_y = (int16_t)((buf[2] << 8) | buf[3]);
    data->accel_z = (int16_t)((buf[4] << 8) | buf[5]);

    data->temp    = (int16_t)((buf[6] << 8) | buf[7]);

    data->gyro_x  = (int16_t)((buf[8] << 8) | buf[9]);
    data->gyro_y  = (int16_t)((buf[10] << 8) | buf[11]);
    data->gyro_z  = (int16_t)((buf[12] << 8) | buf[13]);

    return 0;
}

/**
 * Convert Raw to Scaled Values
 *
 * WHY SCALED: Applications want real units (m/s², °/s, °C),
 * not arbitrary raw counts.
 */
int mpu6050_get_scaled_data(const struct device *dev,
                            struct mpu6050_scaled_data *data)
{
    struct mpu6050_data *drv_data = dev->data;
    struct mpu6050_raw_data raw;
    int ret;

    ret = mpu6050_get_raw_data(dev, &raw);
    if (ret < 0) {
        return ret;
    }

    /**
     * Accelerometer Conversion
     *
     * Formula: accel_g = raw / sensitivity
     *
     * Sensitivity depends on full-scale range:
     *   ±2g:  16384 LSB/g
     *   ±4g:  8192 LSB/g
     *   ±8g:  4096 LSB/g
     *   ±16g: 2048 LSB/g
     *
     * We convert to m/s² by multiplying by 9.81
     */
    float accel_sensitivity = get_accel_sensitivity(drv_data->accel_range);
    data->accel_x = (raw.accel_x / accel_sensitivity) * 9.81f;
    data->accel_y = (raw.accel_y / accel_sensitivity) * 9.81f;
    data->accel_z = (raw.accel_z / accel_sensitivity) * 9.81f;

    /**
     * Gyroscope Conversion
     *
     * Sensitivity depends on full-scale range:
     *   ±250°/s:  131 LSB/(°/s)
     *   ±500°/s:  65.5 LSB/(°/s)
     *   ±1000°/s: 32.8 LSB/(°/s)
     *   ±2000°/s: 16.4 LSB/(°/s)
     */
    float gyro_sensitivity = get_gyro_sensitivity(drv_data->gyro_range);
    data->gyro_x = raw.gyro_x / gyro_sensitivity;
    data->gyro_y = raw.gyro_y / gyro_sensitivity;
    data->gyro_z = raw.gyro_z / gyro_sensitivity;

    /**
     * Temperature Conversion
     *
     * Formula from datasheet:
     * temp_celsius = (raw / 340.0) + 36.53
     */
    data->temp_celsius = (raw.temp / 340.0f) + 36.53f;

    return 0;
}

/**
 * Helper: Get accelerometer sensitivity
 */
static float get_accel_sensitivity(enum mpu6050_accel_range range)
{
    /**
     * Sensitivity values from MPU6050 datasheet
     */
    switch (range) {
        case MPU6050_ACCEL_RANGE_2G:  return 16384.0f;
        case MPU6050_ACCEL_RANGE_4G:  return 8192.0f;
        case MPU6050_ACCEL_RANGE_8G:  return 4096.0f;
        case MPU6050_ACCEL_RANGE_16G: return 2048.0f;
        default: return 16384.0f;
    }
}
```

### Interrupt/Trigger Handling

```c
/**
 * MPU6050 Trigger (Interrupt) Implementation
 *
 * WHY TRIGGERS: Get notified when new data is ready,
 * instead of polling constantly.
 *
 * MPU6050 can generate interrupt on:
 * - Data ready (new sample available)
 * - Motion detected
 * - FIFO overflow
 * - I2C master interrupt
 */

/**
 * GPIO Interrupt Handler
 *
 * Called when MPU6050 INT pin goes active.
 * CONTEXT: ISR - keep it SHORT!
 */
static void mpu6050_gpio_handler(const struct device *gpio_dev,
                                  struct gpio_callback *cb,
                                  uint32_t pins)
{
    struct mpu6050_data *data = CONTAINER_OF(cb, struct mpu6050_data, gpio_cb);

    /**
     * CONTAINER_OF macro:
     *
     * Gets pointer to containing structure from member pointer.
     * cb points to data->gpio_cb, so we can get data.
     *
     * This avoids needing global variables!
     */

    /**
     * Submit work to system workqueue
     *
     * WHY: Can't do I2C in ISR (too slow).
     * Work handler runs in thread context.
     */
    k_work_submit(&data->trigger_work);
}

/**
 * Trigger Work Handler
 *
 * Runs in system workqueue thread.
 * Can do I2C communication safely here.
 */
static void mpu6050_trigger_work_handler(struct k_work *work)
{
    struct mpu6050_data *data = CONTAINER_OF(work, struct mpu6050_data, trigger_work);
    const struct device *dev = data->dev;
    uint8_t int_status;

    /**
     * Read interrupt status register
     *
     * Reading this register clears the interrupt.
     * Tells us what triggered the interrupt.
     */
    if (mpu6050_reg_read(dev, MPU6050_REG_INT_STATUS, &int_status) < 0) {
        LOG_ERR("Failed to read interrupt status");
        return;
    }

    LOG_DBG("Interrupt status: 0x%02X", int_status);

    /**
     * Call user's data ready handler
     */
    if ((int_status & 0x01) && data->data_ready_handler) {
        data->data_ready_handler(dev, data->handler_user_data);
    }
}

/**
 * Set Trigger Handler
 *
 * Application calls this to register for interrupts.
 */
int mpu6050_set_trigger_handler(const struct device *dev,
                                mpu6050_trigger_handler_t handler,
                                void *user_data)
{
    const struct mpu6050_config *cfg = dev->config;
    struct mpu6050_data *data = dev->data;
    int ret;

    /**
     * Store handler
     */
    data->data_ready_handler = handler;
    data->handler_user_data = user_data;

    /**
     * Configure GPIO interrupt
     */
    if (handler != NULL) {
        /**
         * Configure interrupt pin
         *
         * MPU6050 INT pin is active high, push-pull by default.
         * Triggers on rising edge.
         */
        ret = gpio_pin_interrupt_configure_dt(&cfg->int_gpio,
                                               GPIO_INT_EDGE_TO_ACTIVE);
        if (ret < 0) {
            LOG_ERR("Failed to configure GPIO interrupt: %d", ret);
            return ret;
        }

        /**
         * Enable data ready interrupt in MPU6050
         *
         * Bit 0 of INT_ENABLE: Data ready interrupt enable
         */
        ret = mpu6050_reg_write(dev, MPU6050_REG_INT_ENABLE, 0x01);
        if (ret < 0) {
            LOG_ERR("Failed to enable interrupt: %d", ret);
            return ret;
        }

        LOG_INF("Data ready interrupt enabled");
    } else {
        /**
         * Disable interrupts
         */
        gpio_pin_interrupt_configure_dt(&cfg->int_gpio, GPIO_INT_DISABLE);
        mpu6050_reg_write(dev, MPU6050_REG_INT_ENABLE, 0x00);

        LOG_INF("Interrupts disabled");
    }

    return 0;
}
```

### Device Initialization

```c
/**
 * MPU6050 Initialization
 *
 * SEQUENCE:
 * 1. Check dependencies
 * 2. Initialize synchronization
 * 3. Reset device
 * 4. Verify device identity
 * 5. Configure default settings
 * 6. Setup interrupt (if used)
 */
static int mpu6050_init(const struct device *dev)
{
    const struct mpu6050_config *cfg = dev->config;
    struct mpu6050_data *data = dev->data;
    uint8_t who_am_i;
    int ret;

    LOG_INF("Initializing MPU6050");

    /**
     * Step 1: Check I2C bus is ready
     */
    if (!device_is_ready(cfg->i2c_bus)) {
        LOG_ERR("I2C bus not ready");
        return -ENODEV;
    }

    /**
     * Step 2: Initialize data structures
     */
    data->dev = dev;  /* Store back-reference for work handler */
    k_work_init(&data->trigger_work, mpu6050_trigger_work_handler);

    /**
     * Step 3: Reset the device
     *
     * Writing 0x80 to PWR_MGMT_1 triggers internal reset.
     * Device returns to default settings.
     */
    ret = mpu6050_reg_write(dev, MPU6050_REG_PWR_MGMT_1, MPU6050_PWR1_RESET_BIT);
    if (ret < 0) {
        LOG_ERR("Failed to reset device: %d", ret);
        return ret;
    }

    /**
     * Wait for reset to complete
     *
     * Datasheet says up to 100ms for gyro to start.
     * Being conservative here.
     */
    k_sleep(K_MSEC(100));

    /**
     * Step 4: Wake up device
     *
     * Device starts in sleep mode after reset.
     * Clear sleep bit (bit 6) to wake up.
     * Set clock source to PLL with X gyro (best stability).
     */
    ret = mpu6050_reg_write(dev, MPU6050_REG_PWR_MGMT_1, 0x01);
    if (ret < 0) {
        LOG_ERR("Failed to wake device: %d", ret);
        return ret;
    }

    k_sleep(K_MSEC(10));

    /**
     * Step 5: Verify device identity
     *
     * WHO_AM_I should return 0x68.
     * If not, wrong device or communication error.
     */
    ret = mpu6050_reg_read(dev, MPU6050_REG_WHO_AM_I, &who_am_i);
    if (ret < 0) {
        LOG_ERR("Failed to read WHO_AM_I: %d", ret);
        return ret;
    }

    if (who_am_i != MPU6050_WHO_AM_I_VAL) {
        LOG_ERR("Unexpected WHO_AM_I: 0x%02X (expected 0x%02X)",
                who_am_i, MPU6050_WHO_AM_I_VAL);
        return -ENODEV;
    }

    LOG_INF("MPU6050 detected (WHO_AM_I: 0x%02X)", who_am_i);

    /**
     * Step 6: Configure default settings
     *
     * Accel: ±2g (highest sensitivity)
     * Gyro: ±250°/s (highest sensitivity)
     * Sample rate: ~100 Hz
     */
    ret = mpu6050_configure(dev, &default_config);
    if (ret < 0) {
        LOG_ERR("Failed to configure: %d", ret);
        return ret;
    }

    /**
     * Step 7: Setup interrupt GPIO (if specified in DT)
     */
    if (cfg->int_gpio.port != NULL) {
        if (!gpio_is_ready_dt(&cfg->int_gpio)) {
            LOG_ERR("Interrupt GPIO not ready");
            return -ENODEV;
        }

        ret = gpio_pin_configure_dt(&cfg->int_gpio, GPIO_INPUT);
        if (ret < 0) {
            LOG_ERR("Failed to configure GPIO: %d", ret);
            return ret;
        }

        /**
         * Initialize GPIO callback
         *
         * gpio_init_callback() prepares the callback structure.
         * gpio_add_callback() registers it with the GPIO controller.
         */
        gpio_init_callback(&data->gpio_cb, mpu6050_gpio_handler,
                          BIT(cfg->int_gpio.pin));
        gpio_add_callback(cfg->int_gpio.port, &data->gpio_cb);

        LOG_INF("Interrupt GPIO configured");
    }

    LOG_INF("MPU6050 initialization complete");

    return 0;
}
```

---

## uBlox NEO-M8N GPS Driver

### GPS Protocol Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                    NMEA PROTOCOL OVERVIEW                        │
│                                                                  │
│   GPS modules output NMEA sentences - ASCII text over UART.     │
│   Each sentence starts with $ and ends with *XX (checksum).     │
│                                                                  │
│   Common sentences:                                              │
│                                                                  │
│   GGA - Position fix data:                                       │
│   $GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,...*47  │
│         ├──────┼────────┼─┼─────────┼─┼─┼──┼───┼─────┼─┘        │
│         │      │        │ │         │ │ │  │   │     └ Altitude │
│         │      │        │ │         │ │ │  │   └ HDOP           │
│         │      │        │ │         │ │ │  └ Fix type           │
│         │      │        │ │         │ │ └ Satellites used       │
│         │      │        │ │         └─┴ Longitude               │
│         │      │        └─┴ Latitude                            │
│         │      └ Time (HHMMSS)                                  │
│         └ Sentence type                                          │
│                                                                  │
│   RMC - Recommended minimum data:                                │
│   $GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,... │
│                 └ Status: A=valid, V=invalid                     │
│                                                                  │
│   GSA - Satellite status:                                       │
│   $GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1*39              │
│           └ Mode: 1=no fix, 2=2D, 3=3D                          │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### NMEA Parser Implementation

```c
/**
 * NMEA Parser State Machine
 *
 * WHY STATE MACHINE: NMEA sentences arrive byte-by-byte.
 * Need to track where we are in the parsing process.
 */
enum nmea_parser_state {
    NMEA_WAIT_START,    /* Looking for '$' */
    NMEA_IN_SENTENCE,   /* Collecting sentence bytes */
    NMEA_IN_CHECKSUM_1, /* First checksum digit */
    NMEA_IN_CHECKSUM_2, /* Second checksum digit */
};

struct nmea_parser {
    enum nmea_parser_state state;
    char buffer[NMEA_MAX_SENTENCE_LEN];
    size_t index;
    uint8_t calculated_checksum;  /* XOR of all bytes between $ and * */
    uint8_t received_checksum;
};

/**
 * Process one byte through parser
 *
 * CALLED FOR: Each byte received from GPS UART.
 * RETURNS: Complete sentence when found, NULL otherwise.
 */
const char *nmea_parser_feed(struct nmea_parser *parser, char c)
{
    switch (parser->state) {

        case NMEA_WAIT_START:
            /**
             * Looking for sentence start
             */
            if (c == '$') {
                parser->state = NMEA_IN_SENTENCE;
                parser->index = 0;
                parser->calculated_checksum = 0;
            }
            break;

        case NMEA_IN_SENTENCE:
            if (c == '*') {
                /**
                 * End of sentence body, start of checksum
                 */
                parser->buffer[parser->index] = '\0';
                parser->state = NMEA_IN_CHECKSUM_1;
            } else if (c == '\r' || c == '\n') {
                /**
                 * Unexpected end - malformed sentence
                 */
                parser->state = NMEA_WAIT_START;
            } else if (parser->index < NMEA_MAX_SENTENCE_LEN - 1) {
                /**
                 * Accumulate character
                 */
                parser->buffer[parser->index++] = c;
                parser->calculated_checksum ^= c;  /* Update checksum */
            } else {
                /**
                 * Buffer overflow - sentence too long
                 */
                LOG_WRN("NMEA sentence too long");
                parser->state = NMEA_WAIT_START;
            }
            break;

        case NMEA_IN_CHECKSUM_1:
            /**
             * First hex digit of checksum
             */
            parser->received_checksum = hex_char_to_int(c) << 4;
            parser->state = NMEA_IN_CHECKSUM_2;
            break;

        case NMEA_IN_CHECKSUM_2:
            /**
             * Second hex digit - now we can validate
             */
            parser->received_checksum |= hex_char_to_int(c);
            parser->state = NMEA_WAIT_START;

            /**
             * Verify checksum
             */
            if (parser->received_checksum == parser->calculated_checksum) {
                return parser->buffer;  /* Valid sentence! */
            } else {
                LOG_WRN("NMEA checksum mismatch: expected %02X, got %02X",
                        parser->calculated_checksum, parser->received_checksum);
                return NULL;
            }
    }

    return NULL;  /* No complete sentence yet */
}

/**
 * Parse GGA Sentence
 *
 * Extracts position fix data from GGA sentence.
 */
static int parse_gga(const char *sentence, struct gps_position *pos)
{
    /**
     * GGA format:
     * GPGGA,time,lat,N/S,lon,E/W,quality,numSV,hdop,alt,M,sep,M,diffAge,diffStation
     *
     * We use strtok to split by commas.
     */
    char *fields[15];
    char buf[128];
    int field_count = 0;

    /* Copy sentence (strtok modifies string) */
    strncpy(buf, sentence, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /**
     * Split into fields
     */
    char *token = strtok(buf, ",");
    while (token != NULL && field_count < 15) {
        fields[field_count++] = token;
        token = strtok(NULL, ",");
    }

    /**
     * Verify sentence type
     */
    if (field_count < 10 || strncmp(fields[0], "GPGGA", 5) != 0) {
        return -EINVAL;
    }

    /**
     * Parse time (field 1)
     * Format: HHMMSS.sss
     */
    if (strlen(fields[1]) >= 6) {
        pos->time.hour = (fields[1][0] - '0') * 10 + (fields[1][1] - '0');
        pos->time.minute = (fields[1][2] - '0') * 10 + (fields[1][3] - '0');
        pos->time.second = (fields[1][4] - '0') * 10 + (fields[1][5] - '0');
    }

    /**
     * Parse latitude (fields 2-3)
     * Format: DDMM.MMMM,N/S
     *
     * DD = degrees (0-90)
     * MM.MMMM = minutes (0-59.9999)
     *
     * Convert to decimal degrees:
     * decimal = DD + MM.MMMM/60
     */
    if (strlen(fields[2]) > 0) {
        double lat = atof(fields[2]);
        int degrees = (int)(lat / 100);
        double minutes = lat - (degrees * 100);
        pos->latitude = degrees + (minutes / 60.0);

        if (fields[3][0] == 'S') {
            pos->latitude = -pos->latitude;
        }
    }

    /**
     * Parse longitude (fields 4-5)
     * Format: DDDMM.MMMM,E/W
     */
    if (strlen(fields[4]) > 0) {
        double lon = atof(fields[4]);
        int degrees = (int)(lon / 100);
        double minutes = lon - (degrees * 100);
        pos->longitude = degrees + (minutes / 60.0);

        if (fields[5][0] == 'W') {
            pos->longitude = -pos->longitude;
        }
    }

    /**
     * Parse fix quality (field 6)
     * 0 = invalid, 1 = GPS fix, 2 = DGPS, etc.
     */
    pos->fix_quality = atoi(fields[6]);

    /**
     * Parse satellite count (field 7)
     */
    pos->satellites_used = atoi(fields[7]);

    /**
     * Parse HDOP (field 8)
     */
    pos->hdop = atof(fields[8]);

    /**
     * Parse altitude (field 9)
     */
    pos->altitude = atof(fields[9]);

    return 0;
}
```

### GPS Driver Thread

```c
/**
 * GPS Processing Thread
 *
 * WHY DEDICATED THREAD:
 * - GPS sends data continuously (~1 Hz typically)
 * - UART RX happens asynchronously
 * - Need to parse NMEA sentences as they arrive
 * - Callbacks might do significant work
 *
 * Thread runs forever, processing incoming GPS data.
 */
static void gps_thread_fn(void *arg1, void *arg2, void *arg3)
{
    const struct device *dev = arg1;
    struct gps_data *data = dev->data;
    const struct gps_config *cfg = dev->config;
    struct nmea_parser parser = {0};
    uint8_t rx_buf[64];

    LOG_INF("GPS thread started");

    while (1) {
        /**
         * Read from UART
         *
         * This blocks until data is available or timeout.
         * uart_fifo_read() returns number of bytes read.
         */
        int len = uart_fifo_read(cfg->uart, rx_buf, sizeof(rx_buf));

        if (len > 0) {
            /**
             * Process each byte through NMEA parser
             */
            for (int i = 0; i < len; i++) {
                const char *sentence = nmea_parser_feed(&parser, rx_buf[i]);

                if (sentence != NULL) {
                    /**
                     * Got a complete, valid sentence
                     */
                    LOG_DBG("NMEA: %s", sentence);

                    /**
                     * Parse based on sentence type
                     */
                    if (strncmp(sentence, "GPGGA", 5) == 0 ||
                        strncmp(sentence, "GNGGA", 5) == 0) {
                        parse_gga(sentence, &data->position);
                        data->position_valid = true;

                        /**
                         * Notify callback
                         */
                        if (data->position_callback) {
                            data->position_callback(dev, &data->position,
                                                   data->callback_user_data);
                        }
                    }
                    else if (strncmp(sentence, "GPRMC", 5) == 0 ||
                             strncmp(sentence, "GNRMC", 5) == 0) {
                        parse_rmc(sentence, &data->position, &data->velocity);
                    }
                    /* Handle other sentence types... */
                }
            }
        } else {
            /**
             * No data - yield CPU briefly
             */
            k_sleep(K_MSEC(10));
        }
    }
}

/**
 * Create GPS Thread
 *
 * Define thread statically using macros.
 */
K_THREAD_STACK_DEFINE(gps_thread_stack, 1024);

/**
 * Initialize GPS Thread
 *
 * Called from driver init function.
 */
static int gps_start_thread(const struct device *dev)
{
    struct gps_data *data = dev->data;

    /**
     * Create and start the thread
     *
     * Parameters:
     * - Thread object (data->thread)
     * - Stack area
     * - Stack size
     * - Entry function
     * - Three arguments (p1, p2, p3)
     * - Priority (lower = higher priority)
     * - Options (0 = none)
     * - Delay (K_NO_WAIT = start immediately)
     */
    data->thread_id = k_thread_create(&data->thread,
                                       gps_thread_stack,
                                       K_THREAD_STACK_SIZEOF(gps_thread_stack),
                                       gps_thread_fn,
                                       (void *)dev, NULL, NULL,
                                       GPS_THREAD_PRIORITY,
                                       0,
                                       K_NO_WAIT);

    k_thread_name_set(data->thread_id, "gps_rx");

    return 0;
}
```

---

## Ultrasonic Distance Sensor Driver

### HC-SR04 Operation

```
┌─────────────────────────────────────────────────────────────────┐
│                HC-SR04 TIMING DIAGRAM                            │
│                                                                  │
│   1. Trigger pulse (10µs minimum):                              │
│      ┌──┐                                                        │
│   ───┘  └────────────────────────────────────────────────       │
│      10µs                                                        │
│                                                                  │
│   2. Sensor sends 8 ultrasonic pulses (40kHz)                   │
│                                                                  │
│   3. Echo pin shows pulse width = round-trip time:               │
│            ┌──────────────────────┐                              │
│   ─────────┘                      └──────────────────────       │
│            │←── echo_duration_µs ─→│                             │
│                                                                  │
│   4. Calculate distance:                                         │
│      distance_cm = echo_duration_µs / 58                         │
│                                                                  │
│      Why 58?                                                     │
│      - Sound speed ≈ 343 m/s = 0.0343 cm/µs                     │
│      - Round trip: 0.0343 / 2 = 0.01715 cm/µs                   │
│      - 1 / 0.01715 ≈ 58 µs/cm                                   │
│                                                                  │
│   Timing constraints:                                           │
│   - Min trigger: 10µs                                           │
│   - Max echo: ~38ms (660cm range)                               │
│   - Recommended cycle: 60ms minimum                             │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### GPIO-Based Timing

```c
/**
 * Ultrasonic Measurement
 *
 * WHY GPIO TIMING: Echo pulse duration determines distance.
 * Need precise timing of pulse width.
 */
int ultrasonic_measure(const struct device *dev, uint32_t *distance_mm)
{
    const struct ultrasonic_config *cfg = dev->config;
    struct ultrasonic_data *data = dev->data;
    uint32_t start_time, end_time, pulse_width_us;
    int ret;

    /**
     * Step 1: Acquire lock
     *
     * Only one measurement at a time!
     */
    ret = k_mutex_lock(&data->lock, K_MSEC(100));
    if (ret != 0) {
        return -EBUSY;
    }

    /**
     * Step 2: Send trigger pulse
     *
     * Must be at least 10µs high.
     */
    gpio_pin_set_dt(&cfg->trigger_gpio, 1);
    k_busy_wait(10);  /* 10 microseconds */
    gpio_pin_set_dt(&cfg->trigger_gpio, 0);

    /**
     * Step 3: Wait for echo to go high (start of pulse)
     *
     * Timeout if no echo (no object detected or sensor error).
     *
     * WHY POLL: Echo timing is in microseconds.
     * Interrupt latency would affect accuracy.
     */
    start_time = k_cycle_get_32();
    uint32_t timeout_cycles = k_us_to_cyc_ceil32(ECHO_TIMEOUT_US);

    while (gpio_pin_get_dt(&cfg->echo_gpio) == 0) {
        if ((k_cycle_get_32() - start_time) > timeout_cycles) {
            LOG_WRN("Echo start timeout");
            k_mutex_unlock(&data->lock);
            return -ETIMEDOUT;
        }
    }

    /**
     * Step 4: Measure echo pulse width
     *
     * Record time when echo goes high, wait for it to go low.
     */
    start_time = k_cycle_get_32();

    while (gpio_pin_get_dt(&cfg->echo_gpio) == 1) {
        if ((k_cycle_get_32() - start_time) > timeout_cycles) {
            LOG_WRN("Echo end timeout (object too far?)");
            k_mutex_unlock(&data->lock);
            return -ETIMEDOUT;
        }
    }

    end_time = k_cycle_get_32();

    /**
     * Step 5: Calculate pulse width in microseconds
     *
     * k_cyc_to_us_floor32() converts CPU cycles to microseconds.
     */
    pulse_width_us = k_cyc_to_us_floor32(end_time - start_time);

    /**
     * Step 6: Calculate distance
     *
     * distance_mm = (pulse_width_us * 0.343) / 2
     *             = pulse_width_us * 0.1715
     *             = pulse_width_us / 5.83
     *
     * For integer math: distance_mm = pulse_width_us * 1000 / 5830
     *                               = pulse_width_us * 100 / 583
     *
     * Or simpler: distance_mm = pulse_width_us * 10 / 58
     *             (slight error but simple)
     */
    *distance_mm = (pulse_width_us * 10) / 58;

    /**
     * Step 7: Validate range
     *
     * HC-SR04 range: 2cm - 400cm (20mm - 4000mm)
     */
    if (*distance_mm < 20 || *distance_mm > 4000) {
        LOG_DBG("Out of range: %u mm", *distance_mm);
        k_mutex_unlock(&data->lock);
        return -ERANGE;
    }

    k_mutex_unlock(&data->lock);

    LOG_DBG("Distance: %u mm (echo: %u µs)", *distance_mm, pulse_width_us);

    return 0;
}
```

### Filtering for Stability

```c
/**
 * Median Filter Implementation
 *
 * WHY FILTER: Ultrasonic readings can be noisy.
 * - Multi-path reflections
 * - Interference
 * - Moving objects
 *
 * Median filter rejects outliers better than average.
 */

#define FILTER_SIZE 5

struct median_filter {
    uint32_t samples[FILTER_SIZE];
    uint8_t index;
    bool filled;  /* True once we have FILTER_SIZE samples */
};

/**
 * Add sample to filter
 */
static void filter_add_sample(struct median_filter *filter, uint32_t sample)
{
    filter->samples[filter->index] = sample;
    filter->index = (filter->index + 1) % FILTER_SIZE;

    if (filter->index == 0) {
        filter->filled = true;
    }
}

/**
 * Get median value
 *
 * Simple selection algorithm for small arrays.
 */
static uint32_t filter_get_median(struct median_filter *filter)
{
    uint32_t sorted[FILTER_SIZE];
    int count = filter->filled ? FILTER_SIZE : filter->index;

    if (count == 0) {
        return 0;
    }

    /* Copy samples */
    memcpy(sorted, filter->samples, count * sizeof(uint32_t));

    /* Simple bubble sort (OK for small arrays) */
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (sorted[j] > sorted[j + 1]) {
                uint32_t temp = sorted[j];
                sorted[j] = sorted[j + 1];
                sorted[j + 1] = temp;
            }
        }
    }

    /* Return median */
    return sorted[count / 2];
}

/**
 * Filtered measurement
 *
 * Takes multiple readings and returns median.
 */
int ultrasonic_measure_filtered(const struct device *dev,
                                uint32_t *distance_mm)
{
    struct median_filter filter = {0};
    int valid_count = 0;

    /**
     * Take FILTER_SIZE measurements
     */
    for (int i = 0; i < FILTER_SIZE; i++) {
        uint32_t raw_distance;
        int ret = ultrasonic_measure(dev, &raw_distance);

        if (ret == 0) {
            filter_add_sample(&filter, raw_distance);
            valid_count++;
        }

        /**
         * Small delay between measurements
         *
         * HC-SR04 recommends 60ms cycle time to avoid
         * echoes from previous measurement.
         */
        k_sleep(K_MSEC(60));
    }

    if (valid_count == 0) {
        return -ENODATA;
    }

    *distance_mm = filter_get_median(&filter);

    return 0;
}
```

---

## Comparison and Patterns

### Summary Table

| Aspect | MPU6050 (I2C) | GPS (UART) | Ultrasonic (GPIO) |
|--------|---------------|------------|-------------------|
| **Bus** | I2C | UART | GPIO (bit-bang) |
| **Data Format** | Binary registers | ASCII NMEA | Timing pulses |
| **Data Rate** | Up to 1 kHz | 1-10 Hz | ~16 Hz max |
| **Interrupt** | Data ready GPIO | RX interrupt | N/A (polling) |
| **Processing** | Simple math | String parsing | Timing measurement |
| **Thread Needed** | Optional (trigger) | Yes (continuous RX) | No |

### Common Patterns Used

```
1. REGISTER-BASED SENSORS (I2C/SPI):
   ┌──────────────────────────────────────────────────────┐
   │  - Define register addresses as constants            │
   │  - Create read/write helper functions                │
   │  - Burst read for multi-byte data                    │
   │  - Convert raw to scaled values                      │
   │  - Optional interrupt for data ready                 │
   └──────────────────────────────────────────────────────┘

2. STREAM-BASED SENSORS (UART):
   ┌──────────────────────────────────────────────────────┐
   │  - Dedicated processing thread                        │
   │  - State machine parser                               │
   │  - Checksum validation                                │
   │  - Callback notification                              │
   │  - Ring buffer for incoming data                      │
   └──────────────────────────────────────────────────────┘

3. TIMING-BASED SENSORS (GPIO):
   ┌──────────────────────────────────────────────────────┐
   │  - Precise timing with k_cycle_get_32()              │
   │  - Busy-wait for short delays                        │
   │  - Polling for state changes                         │
   │  - Filtering for noise rejection                     │
   │  - Range validation                                   │
   └──────────────────────────────────────────────────────┘
```

### When to Use Each Pattern

```c
/*
 * USE REGISTER-BASED WHEN:
 * - Sensor has addressable registers
 * - I2C or SPI interface
 * - Need random access to configuration
 * - Data is in binary format
 *
 * USE STREAM-BASED WHEN:
 * - Continuous data flow
 * - ASCII/text protocol
 * - Variable message lengths
 * - Need to parse structured data
 *
 * USE TIMING-BASED WHEN:
 * - Information encoded in pulse timing
 * - Simple pulse/echo protocol
 * - No digital communication bus
 * - Microsecond precision needed
 */
```

---

## Summary

Each sensor type requires different techniques:

**MPU6050 (I2C)**:
- Register read/write abstraction
- Burst reads for efficiency
- Data conversion from raw to scaled
- GPIO interrupt for data ready trigger

**GPS (UART)**:
- Dedicated thread for continuous processing
- State machine for NMEA parsing
- Checksum validation
- Callback notification for position updates

**Ultrasonic (GPIO)**:
- Precise timing measurement
- Polling for echo pulse
- Median filtering for noise rejection
- Range validation

All three demonstrate:
- Clean API design
- Proper error handling
- Thread safety where needed
- Device Tree configuration
- Shell commands for debugging
