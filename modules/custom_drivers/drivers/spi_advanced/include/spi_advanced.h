/**
 * @file spi_advanced.h
 * @brief Advanced SPI Driver API
 *
 * This driver provides a comprehensive SPI interface supporting:
 * - All SPI modes (0, 1, 2, 3)
 * - Multiple chip select management
 * - DMA transfers
 * - Async (non-blocking) operations
 * - Full-duplex and half-duplex transfers
 * - Variable word sizes (4-16 bits)
 * - Statistics and diagnostics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SPI_ADVANCED_H_
#define SPI_ADVANCED_H_

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief SPI operating modes (CPOL, CPHA combinations)
 */
enum spi_adv_mode {
    SPI_ADV_MODE_0 = 0,  /**< CPOL=0, CPHA=0 (clock idle low, sample leading) */
    SPI_ADV_MODE_1 = 1,  /**< CPOL=0, CPHA=1 (clock idle low, sample trailing) */
    SPI_ADV_MODE_2 = 2,  /**< CPOL=1, CPHA=0 (clock idle high, sample leading) */
    SPI_ADV_MODE_3 = 3,  /**< CPOL=1, CPHA=1 (clock idle high, sample trailing) */
};

/**
 * @brief SPI bit order
 */
enum spi_adv_bit_order {
    SPI_ADV_MSB_FIRST,   /**< Most significant bit first (default) */
    SPI_ADV_LSB_FIRST,   /**< Least significant bit first */
};

/**
 * @brief SPI chip select control
 */
enum spi_adv_cs_control {
    SPI_ADV_CS_ACTIVE_LOW,   /**< CS active low (default) */
    SPI_ADV_CS_ACTIVE_HIGH,  /**< CS active high */
};

/**
 * @brief SPI transfer type
 */
enum spi_adv_transfer_type {
    SPI_ADV_XFER_FULL_DUPLEX,   /**< Simultaneous TX and RX */
    SPI_ADV_XFER_TX_ONLY,       /**< Transmit only */
    SPI_ADV_XFER_RX_ONLY,       /**< Receive only */
};

/**
 * @brief SPI transfer flags
 */
enum spi_adv_flags {
    SPI_ADV_FLAG_NONE       = 0,
    SPI_ADV_FLAG_HOLD_CS    = BIT(0),  /**< Keep CS asserted after transfer */
    SPI_ADV_FLAG_RELEASE_CS = BIT(1),  /**< Release CS after transfer */
    SPI_ADV_FLAG_USE_DMA    = BIT(2),  /**< Force DMA for this transfer */
    SPI_ADV_FLAG_NO_DMA     = BIT(3),  /**< Disable DMA for this transfer */
};

/**
 * @brief Async transfer callback
 */
typedef void (*spi_adv_callback_t)(const struct device *dev,
                                   int result,
                                   void *user_data);

/**
 * @brief SPI device configuration
 */
struct spi_adv_device_config {
    uint32_t frequency;              /**< Clock frequency in Hz */
    enum spi_adv_mode mode;          /**< SPI mode (0-3) */
    enum spi_adv_bit_order bit_order; /**< Bit order */
    uint8_t word_size;               /**< Word size in bits (4-16) */
    struct gpio_dt_spec cs_gpio;     /**< Chip select GPIO */
    enum spi_adv_cs_control cs_ctrl; /**< CS polarity */
    uint16_t cs_delay_us;            /**< Delay after CS assert */
};

/**
 * @brief SPI transfer descriptor
 */
struct spi_adv_transfer {
    const uint8_t *tx_buf;           /**< TX buffer (NULL for RX only) */
    uint8_t *rx_buf;                 /**< RX buffer (NULL for TX only) */
    size_t len;                      /**< Transfer length in bytes */
    enum spi_adv_transfer_type type; /**< Transfer type */
    enum spi_adv_flags flags;        /**< Transfer flags */
};

/**
 * @brief SPI statistics
 */
struct spi_adv_stats {
    uint32_t tx_bytes;               /**< Total bytes transmitted */
    uint32_t rx_bytes;               /**< Total bytes received */
    uint32_t transfers;              /**< Total number of transfers */
    uint32_t dma_transfers;          /**< Transfers using DMA */
    uint32_t errors;                 /**< Total errors */
    uint32_t timeouts;               /**< Timeout errors */
    uint32_t last_transfer_us;       /**< Last transfer duration */
    uint32_t max_transfer_us;        /**< Maximum transfer duration */
};

/* ========== Initialization ========== */

/**
 * @brief Configure an SPI device
 *
 * @param dev SPI Advanced driver instance
 * @param device_id Logical device ID (0-7)
 * @param cfg Device configuration
 * @return 0 on success, negative errno on failure
 */
int spi_adv_device_configure(const struct device *dev,
                             uint8_t device_id,
                             const struct spi_adv_device_config *cfg);

/**
 * @brief Get device configuration
 *
 * @param dev SPI Advanced driver instance
 * @param device_id Logical device ID
 * @param cfg Pointer to store configuration
 * @return 0 on success, negative errno on failure
 */
int spi_adv_device_config_get(const struct device *dev,
                              uint8_t device_id,
                              struct spi_adv_device_config *cfg);

/* ========== Basic Transfer API ========== */

/**
 * @brief Write data to SPI device
 *
 * @param dev SPI Advanced driver instance
 * @param device_id Target device ID
 * @param data Data to write
 * @param len Number of bytes to write
 * @return Number of bytes written, or negative errno
 */
int spi_adv_write(const struct device *dev, uint8_t device_id,
                  const uint8_t *data, size_t len);

/**
 * @brief Read data from SPI device
 *
 * @param dev SPI Advanced driver instance
 * @param device_id Target device ID
 * @param data Buffer to store read data
 * @param len Number of bytes to read
 * @return Number of bytes read, or negative errno
 */
int spi_adv_read(const struct device *dev, uint8_t device_id,
                 uint8_t *data, size_t len);

/**
 * @brief Full-duplex transfer
 *
 * @param dev SPI Advanced driver instance
 * @param device_id Target device ID
 * @param tx_data Data to transmit
 * @param rx_data Buffer for received data
 * @param len Transfer length
 * @return Number of bytes transferred, or negative errno
 */
int spi_adv_transceive(const struct device *dev, uint8_t device_id,
                       const uint8_t *tx_data, uint8_t *rx_data, size_t len);

/* ========== Register Access API ========== */

/**
 * @brief Write to device register
 *
 * @param dev SPI Advanced driver instance
 * @param device_id Target device ID
 * @param reg_addr Register address
 * @param data Data to write
 * @param len Number of bytes
 * @return 0 on success, negative errno on failure
 */
int spi_adv_reg_write(const struct device *dev, uint8_t device_id,
                      uint8_t reg_addr, const uint8_t *data, size_t len);

/**
 * @brief Read from device register
 *
 * @param dev SPI Advanced driver instance
 * @param device_id Target device ID
 * @param reg_addr Register address
 * @param data Buffer for data
 * @param len Number of bytes to read
 * @return 0 on success, negative errno on failure
 */
int spi_adv_reg_read(const struct device *dev, uint8_t device_id,
                     uint8_t reg_addr, uint8_t *data, size_t len);

/**
 * @brief Write single byte to register
 *
 * @param dev SPI Advanced driver instance
 * @param device_id Target device ID
 * @param reg_addr Register address
 * @param value Value to write
 * @return 0 on success, negative errno on failure
 */
int spi_adv_reg_write_byte(const struct device *dev, uint8_t device_id,
                           uint8_t reg_addr, uint8_t value);

/**
 * @brief Read single byte from register
 *
 * @param dev SPI Advanced driver instance
 * @param device_id Target device ID
 * @param reg_addr Register address
 * @param value Pointer to store value
 * @return 0 on success, negative errno on failure
 */
int spi_adv_reg_read_byte(const struct device *dev, uint8_t device_id,
                          uint8_t reg_addr, uint8_t *value);

/* ========== Advanced Transfer API ========== */

/**
 * @brief Execute transfer descriptor
 *
 * @param dev SPI Advanced driver instance
 * @param device_id Target device ID
 * @param xfer Transfer descriptor
 * @return Number of bytes transferred, or negative errno
 */
int spi_adv_transfer(const struct device *dev, uint8_t device_id,
                     const struct spi_adv_transfer *xfer);

/**
 * @brief Execute multiple transfers (scatter-gather)
 *
 * @param dev SPI Advanced driver instance
 * @param device_id Target device ID
 * @param xfers Array of transfer descriptors
 * @param count Number of transfers
 * @return Total bytes transferred, or negative errno
 */
int spi_adv_transfer_multi(const struct device *dev, uint8_t device_id,
                           const struct spi_adv_transfer *xfers, size_t count);

/* ========== Async API ========== */

#ifdef CONFIG_SPI_ADVANCED_ASYNC
/**
 * @brief Start async write
 *
 * @param dev SPI Advanced driver instance
 * @param device_id Target device ID
 * @param data Data to write
 * @param len Number of bytes
 * @param callback Completion callback
 * @param user_data User data for callback
 * @return 0 on success, negative errno on failure
 */
int spi_adv_write_async(const struct device *dev, uint8_t device_id,
                        const uint8_t *data, size_t len,
                        spi_adv_callback_t callback, void *user_data);

/**
 * @brief Start async read
 *
 * @param dev SPI Advanced driver instance
 * @param device_id Target device ID
 * @param data Buffer for data
 * @param len Number of bytes
 * @param callback Completion callback
 * @param user_data User data for callback
 * @return 0 on success, negative errno on failure
 */
int spi_adv_read_async(const struct device *dev, uint8_t device_id,
                       uint8_t *data, size_t len,
                       spi_adv_callback_t callback, void *user_data);

/**
 * @brief Start async transceive
 *
 * @param dev SPI Advanced driver instance
 * @param device_id Target device ID
 * @param tx_data TX buffer
 * @param rx_data RX buffer
 * @param len Transfer length
 * @param callback Completion callback
 * @param user_data User data for callback
 * @return 0 on success, negative errno on failure
 */
int spi_adv_transceive_async(const struct device *dev, uint8_t device_id,
                             const uint8_t *tx_data, uint8_t *rx_data,
                             size_t len, spi_adv_callback_t callback,
                             void *user_data);

/**
 * @brief Wait for async operation to complete
 *
 * @param dev SPI Advanced driver instance
 * @param timeout Timeout
 * @return 0 on success, negative errno on timeout/error
 */
int spi_adv_async_wait(const struct device *dev, k_timeout_t timeout);

/**
 * @brief Cancel pending async operation
 *
 * @param dev SPI Advanced driver instance
 * @return 0 on success, negative errno on failure
 */
int spi_adv_async_cancel(const struct device *dev);
#endif /* CONFIG_SPI_ADVANCED_ASYNC */

/* ========== Chip Select Control ========== */

/**
 * @brief Assert chip select
 *
 * @param dev SPI Advanced driver instance
 * @param device_id Target device ID
 * @return 0 on success, negative errno on failure
 */
int spi_adv_cs_select(const struct device *dev, uint8_t device_id);

/**
 * @brief Release chip select
 *
 * @param dev SPI Advanced driver instance
 * @param device_id Target device ID
 * @return 0 on success, negative errno on failure
 */
int spi_adv_cs_release(const struct device *dev, uint8_t device_id);

/* ========== Statistics ========== */

/**
 * @brief Get driver statistics
 *
 * @param dev SPI Advanced driver instance
 * @param stats Pointer to store statistics
 * @return 0 on success, negative errno on failure
 */
int spi_adv_stats_get(const struct device *dev, struct spi_adv_stats *stats);

/**
 * @brief Reset statistics
 *
 * @param dev SPI Advanced driver instance
 * @return 0 on success, negative errno on failure
 */
int spi_adv_stats_reset(const struct device *dev);

/* ========== Utility Functions ========== */

/**
 * @brief Set frequency for device
 *
 * @param dev SPI Advanced driver instance
 * @param device_id Target device ID
 * @param frequency New frequency in Hz
 * @return 0 on success, negative errno on failure
 */
int spi_adv_set_frequency(const struct device *dev, uint8_t device_id,
                          uint32_t frequency);

/**
 * @brief Set SPI mode for device
 *
 * @param dev SPI Advanced driver instance
 * @param device_id Target device ID
 * @param mode New SPI mode
 * @return 0 on success, negative errno on failure
 */
int spi_adv_set_mode(const struct device *dev, uint8_t device_id,
                     enum spi_adv_mode mode);

/**
 * @brief Check if device is configured
 *
 * @param dev SPI Advanced driver instance
 * @param device_id Target device ID
 * @return true if configured, false otherwise
 */
bool spi_adv_device_is_configured(const struct device *dev, uint8_t device_id);

#ifdef __cplusplus
}
#endif

#endif /* SPI_ADVANCED_H_ */
