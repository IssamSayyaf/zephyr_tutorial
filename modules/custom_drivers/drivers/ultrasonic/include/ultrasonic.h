/**
 * @file ultrasonic.h
 * @brief Ultrasonic Distance Sensor Driver API
 *
 * This driver provides access to HC-SR04 style ultrasonic sensors:
 * - Distance measurement in centimeters and millimeters
 * - Configurable trigger and echo GPIO pins
 * - Optional median filtering for noise reduction
 * - Temperature compensation option
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ULTRASONIC_H_
#define ULTRASONIC_H_

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Measurement result structure
 */
struct ultrasonic_result {
    uint32_t distance_mm;    /**< Distance in millimeters */
    uint32_t distance_cm;    /**< Distance in centimeters */
    uint32_t echo_time_us;   /**< Echo round-trip time in microseconds */
    bool valid;              /**< Measurement validity */
    int error;               /**< Error code if not valid */
};

/**
 * @brief Sensor status
 */
enum ultrasonic_status {
    ULTRASONIC_STATUS_OK = 0,        /**< Sensor OK */
    ULTRASONIC_STATUS_TIMEOUT = 1,   /**< No echo received */
    ULTRASONIC_STATUS_TOO_CLOSE = 2, /**< Object too close */
    ULTRASONIC_STATUS_TOO_FAR = 3,   /**< Object too far */
    ULTRASONIC_STATUS_ERROR = 4,     /**< General error */
};

/**
 * @brief Callback type for async measurements
 */
typedef void (*ultrasonic_callback_t)(const struct device *dev,
                                      const struct ultrasonic_result *result,
                                      void *user_data);

/* ========== API Functions ========== */

/**
 * @brief Measure distance (blocking)
 *
 * @param dev Device instance
 * @param result Pointer to store result
 * @return 0 on success, negative errno on failure
 */
int ultrasonic_measure(const struct device *dev,
                       struct ultrasonic_result *result);

/**
 * @brief Get distance in centimeters (convenience function)
 *
 * @param dev Device instance
 * @param distance_cm Pointer to store distance in cm
 * @return 0 on success, negative errno on failure
 */
int ultrasonic_get_distance_cm(const struct device *dev,
                               uint32_t *distance_cm);

/**
 * @brief Get distance in millimeters
 *
 * @param dev Device instance
 * @param distance_mm Pointer to store distance in mm
 * @return 0 on success, negative errno on failure
 */
int ultrasonic_get_distance_mm(const struct device *dev,
                               uint32_t *distance_mm);

/**
 * @brief Get echo time in microseconds
 *
 * @param dev Device instance
 * @param time_us Pointer to store echo time
 * @return 0 on success, negative errno on failure
 */
int ultrasonic_get_echo_time(const struct device *dev,
                             uint32_t *time_us);

/**
 * @brief Measure with median filtering
 *
 * @param dev Device instance
 * @param samples Number of samples (must be odd, 3-11)
 * @param result Pointer to store result
 * @return 0 on success, negative errno on failure
 */
int ultrasonic_measure_filtered(const struct device *dev,
                                uint8_t samples,
                                struct ultrasonic_result *result);

/**
 * @brief Set temperature for speed of sound compensation
 *
 * @param dev Device instance
 * @param temp_celsius Temperature in Celsius
 * @return 0 on success, negative errno on failure
 */
int ultrasonic_set_temperature(const struct device *dev,
                               float temp_celsius);

/**
 * @brief Set callback for async measurements
 *
 * @param dev Device instance
 * @param callback Callback function
 * @param user_data User data passed to callback
 * @return 0 on success, negative errno on failure
 */
int ultrasonic_set_callback(const struct device *dev,
                            ultrasonic_callback_t callback,
                            void *user_data);

/**
 * @brief Start continuous measurements
 *
 * @param dev Device instance
 * @param interval_ms Interval between measurements
 * @return 0 on success, negative errno on failure
 */
int ultrasonic_start_continuous(const struct device *dev,
                                uint32_t interval_ms);

/**
 * @brief Stop continuous measurements
 *
 * @param dev Device instance
 * @return 0 on success, negative errno on failure
 */
int ultrasonic_stop_continuous(const struct device *dev);

/**
 * @brief Check if object is in range
 *
 * @param dev Device instance
 * @param min_cm Minimum distance in cm
 * @param max_cm Maximum distance in cm
 * @return true if object is in range, false otherwise
 */
bool ultrasonic_in_range(const struct device *dev,
                         uint32_t min_cm, uint32_t max_cm);

/**
 * @brief Get sensor status
 *
 * @param dev Device instance
 * @return Sensor status
 */
enum ultrasonic_status ultrasonic_get_status(const struct device *dev);

/**
 * @brief Get minimum measurable distance
 *
 * @param dev Device instance
 * @return Minimum distance in cm
 */
uint32_t ultrasonic_get_min_range(const struct device *dev);

/**
 * @brief Get maximum measurable distance
 *
 * @param dev Device instance
 * @return Maximum distance in cm
 */
uint32_t ultrasonic_get_max_range(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* ULTRASONIC_H_ */
