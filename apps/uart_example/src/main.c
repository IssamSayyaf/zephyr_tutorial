/**
 * @file main.c
 * @brief UART Advanced Driver Example
 *
 * Demonstrates UART communication in different modes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include "uart_advanced.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#if DT_NODE_EXISTS(DT_NODELABEL(uart_adv0))
#define UART_ADV_NODE DT_NODELABEL(uart_adv0)
#else
#error "No uart_adv0 node found in devicetree"
#endif

static void rx_callback(const struct device *dev,
                        struct uart_adv_event *evt,
                        void *user_data)
{
    if (evt->type == UART_ADV_EVT_RX_RDY) {
        LOG_INF("RX event: %zu bytes available", evt->data.rx.len);
    } else if (evt->type == UART_ADV_EVT_TX_DONE) {
        LOG_INF("TX complete");
    }
}

int main(void)
{
    const struct device *dev = DEVICE_DT_GET(UART_ADV_NODE);
    int ret;

    LOG_INF("UART Advanced Driver Example");
    LOG_INF("============================");

    if (!device_is_ready(dev)) {
        LOG_ERR("UART device not ready");
        return -ENODEV;
    }

    /* Configure UART */
    struct uart_adv_config cfg = {
        .baudrate = 115200,
        .parity = UART_ADV_PARITY_NONE,
        .stop_bits = UART_ADV_STOPBITS_1,
        .data_bits = UART_ADV_DATABITS_8,
        .flow_ctrl = UART_ADV_FLOW_CTRL_NONE,
    };

    ret = uart_adv_configure(dev, &cfg);
    if (ret < 0) {
        LOG_ERR("Configuration failed: %d", ret);
        return ret;
    }

    LOG_INF("UART configured: 115200 8N1");

    /* Set callback for events */
    uart_adv_callback_set(dev, rx_callback, NULL);

    /* Demonstrate polling mode */
    LOG_INF("");
    LOG_INF("=== Polling Mode Demo ===");

    const char *msg = "Hello from UART!\r\n";
    ret = uart_adv_poll_write(dev, (uint8_t *)msg, strlen(msg));
    LOG_INF("Sent %d bytes", ret);

    /* Demonstrate printf function */
    uart_adv_printf(dev, "Counter test: %d, %d, %d\r\n", 1, 2, 3);

    /* Switch to interrupt mode */
    LOG_INF("");
    LOG_INF("=== Interrupt Mode Demo ===");

    uart_adv_mode_set(dev, UART_ADV_MODE_INT);
    uart_adv_irq_rx_enable(dev);

    LOG_INF("Switched to interrupt mode");
    LOG_INF("Type characters to echo...");

    /* Echo loop */
    while (1) {
        uint8_t buf[64];
        int len;

        /* Check for available data */
        size_t avail = uart_adv_irq_rx_available(dev);
        if (avail > 0) {
            len = uart_adv_irq_read(dev, buf, sizeof(buf));
            if (len > 0) {
                LOG_INF("Received %d bytes", len);
                /* Echo back */
                uart_adv_irq_write(dev, buf, len);
            }
        }

        /* Print stats periodically */
        static int counter = 0;
        if (++counter >= 100) {
            counter = 0;
            struct uart_adv_stats stats;
            uart_adv_stats_get(dev, &stats);
            LOG_INF("Stats: TX=%u, RX=%u bytes",
                    stats.tx_bytes, stats.rx_bytes);
        }

        k_sleep(K_MSEC(100));
    }

    return 0;
}
