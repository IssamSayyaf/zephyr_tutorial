/**
 * @file ublox_neo_m8n.h
 * @brief uBlox NEO-M8N GPS/GNSS Module Driver API
 *
 * This driver provides access to the uBlox NEO-M8N GPS module:
 * - NMEA sentence parsing (GGA, RMC, GSA, VTG)
 * - Position, velocity, and time data
 * - Satellite information
 * - Fix quality and accuracy
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef UBLOX_NEO_M8N_H_
#define UBLOX_NEO_M8N_H_

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== Types ========== */

/**
 * @brief GPS fix type
 */
enum gps_fix_type {
    GPS_FIX_NONE = 0,       /**< No fix */
    GPS_FIX_2D = 1,         /**< 2D fix */
    GPS_FIX_3D = 2,         /**< 3D fix */
};

/**
 * @brief GPS fix quality (from GGA)
 */
enum gps_fix_quality {
    GPS_QUALITY_INVALID = 0,     /**< Invalid fix */
    GPS_QUALITY_GPS = 1,         /**< GPS fix */
    GPS_QUALITY_DGPS = 2,        /**< Differential GPS fix */
    GPS_QUALITY_PPS = 3,         /**< PPS fix */
    GPS_QUALITY_RTK = 4,         /**< RTK fix */
    GPS_QUALITY_FLOAT_RTK = 5,   /**< Float RTK */
    GPS_QUALITY_ESTIMATED = 6,   /**< Estimated */
    GPS_QUALITY_MANUAL = 7,      /**< Manual input */
    GPS_QUALITY_SIMULATION = 8,  /**< Simulation */
};

/**
 * @brief GPS time structure
 */
struct gps_time {
    uint16_t year;      /**< Year (2000-2099) */
    uint8_t month;      /**< Month (1-12) */
    uint8_t day;        /**< Day (1-31) */
    uint8_t hour;       /**< Hour (0-23) */
    uint8_t minute;     /**< Minute (0-59) */
    uint8_t second;     /**< Second (0-59) */
    uint16_t millisec;  /**< Milliseconds (0-999) */
    bool valid;         /**< Time validity */
};

/**
 * @brief GPS position structure
 */
struct gps_position {
    double latitude;    /**< Latitude in degrees (positive = N) */
    double longitude;   /**< Longitude in degrees (positive = E) */
    float altitude;     /**< Altitude above MSL in meters */
    float geoid_sep;    /**< Geoid separation in meters */
    bool valid;         /**< Position validity */
};

/**
 * @brief GPS velocity structure
 */
struct gps_velocity {
    float speed_knots;  /**< Speed in knots */
    float speed_kmh;    /**< Speed in km/h */
    float speed_mps;    /**< Speed in m/s */
    float course;       /**< Course over ground in degrees */
    bool valid;         /**< Velocity validity */
};

/**
 * @brief GPS accuracy/DOP structure
 */
struct gps_accuracy {
    float hdop;         /**< Horizontal DOP */
    float vdop;         /**< Vertical DOP */
    float pdop;         /**< Position DOP */
    bool valid;         /**< DOP validity */
};

/**
 * @brief Satellite information
 */
struct gps_satellite {
    uint8_t prn;        /**< PRN number */
    uint8_t elevation;  /**< Elevation in degrees */
    uint16_t azimuth;   /**< Azimuth in degrees */
    uint8_t snr;        /**< Signal-to-noise ratio in dB */
    bool used;          /**< Used in fix calculation */
};

/**
 * @brief Complete GPS data structure
 */
struct gps_data {
    struct gps_time time;
    struct gps_position position;
    struct gps_velocity velocity;
    struct gps_accuracy accuracy;

    enum gps_fix_type fix_type;
    enum gps_fix_quality fix_quality;
    uint8_t satellites_used;
    uint8_t satellites_visible;

    uint32_t timestamp;  /**< System timestamp when data was received */
    bool fix_valid;      /**< Overall fix validity */
};

/**
 * @brief GPS callback type
 */
typedef void (*gps_callback_t)(const struct device *dev,
                               const struct gps_data *data,
                               void *user_data);

/* ========== API Functions ========== */

/**
 * @brief Start GPS data acquisition
 *
 * @param dev Device instance
 * @return 0 on success, negative errno on failure
 */
int gps_start(const struct device *dev);

/**
 * @brief Stop GPS data acquisition
 *
 * @param dev Device instance
 * @return 0 on success, negative errno on failure
 */
int gps_stop(const struct device *dev);

/**
 * @brief Check if GPS is running
 *
 * @param dev Device instance
 * @return true if running, false otherwise
 */
bool gps_is_running(const struct device *dev);

/**
 * @brief Get latest GPS data
 *
 * @param dev Device instance
 * @param data Pointer to store GPS data
 * @return 0 on success, negative errno on failure
 */
int gps_get_data(const struct device *dev, struct gps_data *data);

/**
 * @brief Get current position
 *
 * @param dev Device instance
 * @param lat Pointer to store latitude
 * @param lon Pointer to store longitude
 * @param alt Pointer to store altitude (optional)
 * @return 0 on success, negative errno on failure
 */
int gps_get_position(const struct device *dev,
                     double *lat, double *lon, float *alt);

/**
 * @brief Get current velocity
 *
 * @param dev Device instance
 * @param speed Pointer to store speed (m/s)
 * @param course Pointer to store course (degrees)
 * @return 0 on success, negative errno on failure
 */
int gps_get_velocity(const struct device *dev,
                     float *speed, float *course);

/**
 * @brief Get current time
 *
 * @param dev Device instance
 * @param time Pointer to store time
 * @return 0 on success, negative errno on failure
 */
int gps_get_time(const struct device *dev, struct gps_time *time);

/**
 * @brief Get fix status
 *
 * @param dev Device instance
 * @param fix_type Pointer to store fix type
 * @param quality Pointer to store fix quality
 * @param satellites Pointer to store satellites used
 * @return 0 on success, negative errno on failure
 */
int gps_get_fix_status(const struct device *dev,
                       enum gps_fix_type *fix_type,
                       enum gps_fix_quality *quality,
                       uint8_t *satellites);

/**
 * @brief Check if we have a valid fix
 *
 * @param dev Device instance
 * @return true if fix is valid, false otherwise
 */
bool gps_has_fix(const struct device *dev);

/**
 * @brief Set data callback
 *
 * @param dev Device instance
 * @param callback Callback function
 * @param user_data User data passed to callback
 * @return 0 on success, negative errno on failure
 */
int gps_set_callback(const struct device *dev,
                     gps_callback_t callback,
                     void *user_data);

/**
 * @brief Set update rate
 *
 * @param dev Device instance
 * @param rate_hz Update rate in Hz (1-10)
 * @return 0 on success, negative errno on failure
 */
int gps_set_update_rate(const struct device *dev, uint8_t rate_hz);

/**
 * @brief Configure GNSS constellation
 *
 * @param dev Device instance
 * @param gps Enable GPS
 * @param glonass Enable GLONASS
 * @param galileo Enable Galileo
 * @param beidou Enable BeiDou
 * @return 0 on success, negative errno on failure
 */
int gps_configure_gnss(const struct device *dev,
                       bool gps, bool glonass,
                       bool galileo, bool beidou);

/**
 * @brief Send raw NMEA command
 *
 * @param dev Device instance
 * @param nmea NMEA sentence (without $ and checksum)
 * @return 0 on success, negative errno on failure
 */
int gps_send_nmea(const struct device *dev, const char *nmea);

/**
 * @brief Cold start the GPS
 *
 * @param dev Device instance
 * @return 0 on success, negative errno on failure
 */
int gps_cold_start(const struct device *dev);

/**
 * @brief Warm start the GPS
 *
 * @param dev Device instance
 * @return 0 on success, negative errno on failure
 */
int gps_warm_start(const struct device *dev);

/**
 * @brief Hot start the GPS
 *
 * @param dev Device instance
 * @return 0 on success, negative errno on failure
 */
int gps_hot_start(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* UBLOX_NEO_M8N_H_ */
