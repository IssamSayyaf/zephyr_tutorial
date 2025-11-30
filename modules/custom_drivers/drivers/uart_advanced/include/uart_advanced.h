/**
 * @file uart_advanced.h
 * @brief Advanced UART Driver API
 *
 * This driver provides a comprehensive UART interface supporting:
 * - Polling mode (blocking)
 * - Interrupt mode (non-blocking with callbacks)
 * - Async/DMA mode (zero-copy transfers)
 * - Hardware flow control (RTS/CTS)
 * - Ring buffer management
 * - Error handling and statistics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef UART_ADVANCED_H_
#define UART_ADVANCED_H_

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief UART operating modes
 */
enum uart_adv_mode {
    UART_ADV_MODE_POLL,      /**< Polling mode (blocking) */
    UART_ADV_MODE_INT,       /**< Interrupt mode (non-blocking) */
    UART_ADV_MODE_ASYNC,     /**< Async/DMA mode */
};

/**
 * @brief UART parity options
 */
enum uart_adv_parity {
    UART_ADV_PARITY_NONE,
    UART_ADV_PARITY_ODD,
    UART_ADV_PARITY_EVEN,
};

/**
 * @brief UART stop bits
 */
enum uart_adv_stopbits {
    UART_ADV_STOPBITS_1,
    UART_ADV_STOPBITS_2,
};

/**
 * @brief UART data bits
 */
enum uart_adv_databits {
    UART_ADV_DATABITS_5 = 5,
    UART_ADV_DATABITS_6 = 6,
    UART_ADV_DATABITS_7 = 7,
    UART_ADV_DATABITS_8 = 8,
};

/**
 * @brief UART flow control options
 */
enum uart_adv_flow_ctrl {
    UART_ADV_FLOW_CTRL_NONE,
    UART_ADV_FLOW_CTRL_RTS_CTS,
};

/**
 * @brief UART event types for callbacks
 */
enum uart_adv_event_type {
    UART_ADV_EVT_TX_DONE,     /**< Transmission complete */
    UART_ADV_EVT_TX_ABORTED,  /**< Transmission aborted */
    UART_ADV_EVT_RX_RDY,      /**< Data received */
    UART_ADV_EVT_RX_BUF_REQ,  /**< Buffer request (async mode) */
    UART_ADV_EVT_RX_BUF_REL,  /**< Buffer released (async mode) */
    UART_ADV_EVT_RX_STOPPED,  /**< Reception stopped */
    UART_ADV_EVT_ERROR,       /**< Error occurred */
};

/**
 * @brief UART error flags
 */
enum uart_adv_error {
    UART_ADV_ERROR_NONE      = 0,
    UART_ADV_ERROR_OVERRUN   = BIT(0),
    UART_ADV_ERROR_PARITY    = BIT(1),
    UART_ADV_ERROR_FRAMING   = BIT(2),
    UART_ADV_ERROR_BREAK     = BIT(3),
};

/**
 * @brief UART configuration structure
 */
struct uart_adv_config {
    uint32_t baudrate;
    enum uart_adv_parity parity;
    enum uart_adv_stopbits stop_bits;
    enum uart_adv_databits data_bits;
    enum uart_adv_flow_ctrl flow_ctrl;
};

/**
 * @brief UART event structure
 */
struct uart_adv_event {
    enum uart_adv_event_type type;
    union {
        struct {
            uint8_t *buf;
            size_t len;
        } rx;
        struct {
            uint8_t *buf;
            size_t len;
        } tx;
        struct {
            enum uart_adv_error error;
        } error;
    } data;
};

/**
 * @brief UART statistics structure
 */
struct uart_adv_stats {
    uint32_t tx_bytes;
    uint32_t rx_bytes;
    uint32_t tx_errors;
    uint32_t rx_errors;
    uint32_t overrun_errors;
    uint32_t parity_errors;
    uint32_t framing_errors;
};

/**
 * @brief UART event callback type
 *
 * @param dev Device instance
 * @param evt Event data
 * @param user_data User data passed during callback registration
 */
typedef void (*uart_adv_callback_t)(const struct device *dev,
                                    struct uart_adv_event *evt,
                                    void *user_data);

/**
 * @brief Configure UART parameters
 *
 * @param dev Device instance
 * @param cfg Configuration parameters
 * @return 0 on success, negative errno on failure
 */
int uart_adv_configure(const struct device *dev,
                       const struct uart_adv_config *cfg);

/**
 * @brief Get current UART configuration
 *
 * @param dev Device instance
 * @param cfg Pointer to store configuration
 * @return 0 on success, negative errno on failure
 */
int uart_adv_config_get(const struct device *dev,
                        struct uart_adv_config *cfg);

/**
 * @brief Set operating mode
 *
 * @param dev Device instance
 * @param mode Operating mode
 * @return 0 on success, negative errno on failure
 */
int uart_adv_mode_set(const struct device *dev, enum uart_adv_mode mode);

/**
 * @brief Set event callback
 *
 * @param dev Device instance
 * @param callback Callback function (NULL to disable)
 * @param user_data User data passed to callback
 * @return 0 on success, negative errno on failure
 */
int uart_adv_callback_set(const struct device *dev,
                          uart_adv_callback_t callback,
                          void *user_data);

/* ========== Polling Mode API ========== */

/**
 * @brief Send single byte (blocking)
 *
 * @param dev Device instance
 * @param byte Byte to send
 * @return 0 on success, negative errno on failure
 */
int uart_adv_poll_out(const struct device *dev, uint8_t byte);

/**
 * @brief Receive single byte (blocking)
 *
 * @param dev Device instance
 * @param byte Pointer to store received byte
 * @param timeout Timeout (K_NO_WAIT, K_FOREVER, or K_MSEC(x))
 * @return 0 on success, -EAGAIN if no data, negative errno on failure
 */
int uart_adv_poll_in(const struct device *dev, uint8_t *byte,
                     k_timeout_t timeout);

/**
 * @brief Send buffer (blocking)
 *
 * @param dev Device instance
 * @param buf Data to send
 * @param len Number of bytes to send
 * @return Number of bytes sent, or negative errno on failure
 */
int uart_adv_poll_write(const struct device *dev,
                        const uint8_t *buf, size_t len);

/**
 * @brief Receive buffer (blocking)
 *
 * @param dev Device instance
 * @param buf Buffer to store data
 * @param len Maximum bytes to receive
 * @param timeout Timeout
 * @return Number of bytes received, or negative errno on failure
 */
int uart_adv_poll_read(const struct device *dev,
                       uint8_t *buf, size_t len,
                       k_timeout_t timeout);

/* ========== Interrupt Mode API ========== */

/**
 * @brief Enable interrupt-driven receive
 *
 * @param dev Device instance
 * @return 0 on success, negative errno on failure
 */
int uart_adv_irq_rx_enable(const struct device *dev);

/**
 * @brief Disable interrupt-driven receive
 *
 * @param dev Device instance
 * @return 0 on success, negative errno on failure
 */
int uart_adv_irq_rx_disable(const struct device *dev);

/**
 * @brief Send data via interrupt mode
 *
 * @param dev Device instance
 * @param buf Data to send
 * @param len Number of bytes to send
 * @return Number of bytes queued, or negative errno on failure
 */
int uart_adv_irq_write(const struct device *dev,
                       const uint8_t *buf, size_t len);

/**
 * @brief Read data from interrupt RX buffer
 *
 * @param dev Device instance
 * @param buf Buffer to store data
 * @param len Maximum bytes to read
 * @return Number of bytes read, or negative errno on failure
 */
int uart_adv_irq_read(const struct device *dev,
                      uint8_t *buf, size_t len);

/**
 * @brief Get number of bytes available in RX buffer
 *
 * @param dev Device instance
 * @return Number of bytes available
 */
size_t uart_adv_irq_rx_available(const struct device *dev);

/* ========== Async/DMA Mode API ========== */

/**
 * @brief Start async transmission
 *
 * @param dev Device instance
 * @param buf Data to send (must remain valid until TX_DONE event)
 * @param len Number of bytes to send
 * @param timeout Timeout for the operation
 * @return 0 on success, negative errno on failure
 */
int uart_adv_async_tx(const struct device *dev,
                      const uint8_t *buf, size_t len,
                      k_timeout_t timeout);

/**
 * @brief Abort async transmission
 *
 * @param dev Device instance
 * @return 0 on success, negative errno on failure
 */
int uart_adv_async_tx_abort(const struct device *dev);

/**
 * @brief Enable async reception
 *
 * @param dev Device instance
 * @param buf Initial receive buffer
 * @param len Buffer size
 * @param timeout Timeout between characters
 * @return 0 on success, negative errno on failure
 */
int uart_adv_async_rx_enable(const struct device *dev,
                             uint8_t *buf, size_t len,
                             k_timeout_t timeout);

/**
 * @brief Provide new buffer for async reception
 *
 * @param dev Device instance
 * @param buf New buffer
 * @param len Buffer size
 * @return 0 on success, negative errno on failure
 */
int uart_adv_async_rx_buf_rsp(const struct device *dev,
                              uint8_t *buf, size_t len);

/**
 * @brief Disable async reception
 *
 * @param dev Device instance
 * @return 0 on success, negative errno on failure
 */
int uart_adv_async_rx_disable(const struct device *dev);

/* ========== Utility Functions ========== */

/**
 * @brief Flush TX buffer
 *
 * @param dev Device instance
 * @return 0 on success, negative errno on failure
 */
int uart_adv_tx_flush(const struct device *dev);

/**
 * @brief Flush RX buffer
 *
 * @param dev Device instance
 * @return 0 on success, negative errno on failure
 */
int uart_adv_rx_flush(const struct device *dev);

/**
 * @brief Get driver statistics
 *
 * @param dev Device instance
 * @param stats Pointer to store statistics
 * @return 0 on success, negative errno on failure
 */
int uart_adv_stats_get(const struct device *dev,
                       struct uart_adv_stats *stats);

/**
 * @brief Reset driver statistics
 *
 * @param dev Device instance
 * @return 0 on success, negative errno on failure
 */
int uart_adv_stats_reset(const struct device *dev);

/**
 * @brief Send formatted string (printf-style)
 *
 * @param dev Device instance
 * @param fmt Format string
 * @param ... Arguments
 * @return Number of bytes sent, or negative errno on failure
 */
int uart_adv_printf(const struct device *dev, const char *fmt, ...);

/**
 * @brief Read line (until newline or buffer full)
 *
 * @param dev Device instance
 * @param buf Buffer to store line
 * @param len Maximum buffer size
 * @param timeout Timeout
 * @return Number of bytes read (excluding null terminator),
 *         or negative errno on failure
 */
int uart_adv_readline(const struct device *dev,
                      char *buf, size_t len,
                      k_timeout_t timeout);

#ifdef __cplusplus
}
#endif

#endif /* UART_ADVANCED_H_ */
