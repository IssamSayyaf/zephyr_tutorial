/**
 * @file uart_advanced_shell.c
 * @brief Shell commands for UART Advanced Driver
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/device.h>
#include <stdlib.h>
#include <string.h>

#include "uart_advanced.h"

#define UART_ADV_DEVICE DT_NODELABEL(uart_adv0)

static const struct device *get_uart_adv_dev(void)
{
#if DT_NODE_EXISTS(UART_ADV_DEVICE)
    return DEVICE_DT_GET(UART_ADV_DEVICE);
#else
    return NULL;
#endif
}

static int cmd_uart_status(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_uart_adv_dev();
    struct uart_adv_stats stats;
    struct uart_adv_config cfg;

    if (dev == NULL) {
        shell_error(sh, "UART device not found");
        return -ENODEV;
    }

    if (!device_is_ready(dev)) {
        shell_error(sh, "UART device not ready");
        return -ENODEV;
    }

    uart_adv_config_get(dev, &cfg);
    uart_adv_stats_get(dev, &stats);

    shell_print(sh, "UART Advanced Driver Status");
    shell_print(sh, "===========================");
    shell_print(sh, "Configuration:");
    shell_print(sh, "  Baudrate:    %u", cfg.baudrate);
    shell_print(sh, "  Data bits:   %u", cfg.data_bits);
    shell_print(sh, "  Parity:      %s",
                cfg.parity == UART_ADV_PARITY_NONE ? "none" :
                cfg.parity == UART_ADV_PARITY_ODD ? "odd" : "even");
    shell_print(sh, "  Stop bits:   %s",
                cfg.stop_bits == UART_ADV_STOPBITS_1 ? "1" : "2");
    shell_print(sh, "  Flow ctrl:   %s",
                cfg.flow_ctrl == UART_ADV_FLOW_CTRL_NONE ? "none" : "RTS/CTS");

    shell_print(sh, "");
    shell_print(sh, "Statistics:");
    shell_print(sh, "  TX bytes:    %u", stats.tx_bytes);
    shell_print(sh, "  RX bytes:    %u", stats.rx_bytes);
    shell_print(sh, "  TX errors:   %u", stats.tx_errors);
    shell_print(sh, "  RX errors:   %u", stats.rx_errors);
    shell_print(sh, "  Overruns:    %u", stats.overrun_errors);
    shell_print(sh, "  Parity err:  %u", stats.parity_errors);
    shell_print(sh, "  Framing err: %u", stats.framing_errors);

    return 0;
}

static int cmd_uart_send(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_uart_adv_dev();
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "UART device not available");
        return -ENODEV;
    }

    if (argc < 2) {
        shell_error(sh, "Usage: uart send <text>");
        return -EINVAL;
    }

    /* Concatenate all arguments */
    for (int i = 1; i < argc; i++) {
        ret = uart_adv_poll_write(dev, (uint8_t *)argv[i], strlen(argv[i]));
        if (ret < 0) {
            shell_error(sh, "Send failed: %d", ret);
            return ret;
        }
        if (i < argc - 1) {
            uart_adv_poll_out(dev, ' ');
        }
    }
    uart_adv_poll_out(dev, '\r');
    uart_adv_poll_out(dev, '\n');

    shell_print(sh, "Sent successfully");
    return 0;
}

static int cmd_uart_recv(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_uart_adv_dev();
    uint8_t buf[64];
    int timeout_ms = 1000;
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "UART device not available");
        return -ENODEV;
    }

    if (argc > 1) {
        timeout_ms = atoi(argv[1]);
    }

    shell_print(sh, "Receiving for %d ms...", timeout_ms);

    ret = uart_adv_poll_read(dev, buf, sizeof(buf) - 1, K_MSEC(timeout_ms));
    if (ret > 0) {
        buf[ret] = '\0';
        shell_print(sh, "Received %d bytes: %s", ret, buf);
        shell_hexdump(sh, buf, ret);
    } else if (ret == -ETIMEDOUT || ret == -EAGAIN) {
        shell_print(sh, "No data received");
    } else {
        shell_error(sh, "Receive failed: %d", ret);
        return ret;
    }

    return 0;
}

static int cmd_uart_loopback(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_uart_adv_dev();
    uint8_t tx_data[] = "UART Loopback Test\r\n";
    uint8_t rx_data[32] = {0};
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "UART device not available");
        return -ENODEV;
    }

    shell_print(sh, "Running loopback test (connect TX to RX)...");

    ret = uart_adv_poll_write(dev, tx_data, sizeof(tx_data) - 1);
    if (ret < 0) {
        shell_error(sh, "TX failed: %d", ret);
        return ret;
    }

    k_msleep(100);

    ret = uart_adv_poll_read(dev, rx_data, sizeof(rx_data), K_MSEC(500));
    if (ret > 0) {
        shell_print(sh, "RX: %d bytes", ret);
        if (memcmp(tx_data, rx_data, ret) == 0) {
            shell_print(sh, "Loopback test PASSED");
        } else {
            shell_error(sh, "Loopback test FAILED - data mismatch");
            shell_hexdump(sh, rx_data, ret);
        }
    } else {
        shell_error(sh, "Loopback test FAILED - no data received");
    }

    return 0;
}

static int cmd_uart_config(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_uart_adv_dev();
    struct uart_adv_config cfg;
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "UART device not available");
        return -ENODEV;
    }

    if (argc < 2) {
        shell_print(sh, "Usage: uart config <baudrate> [data_bits] [parity] [stop_bits]");
        shell_print(sh, "  parity: none, odd, even");
        shell_print(sh, "  stop_bits: 1, 2");
        return -EINVAL;
    }

    /* Get current config as base */
    uart_adv_config_get(dev, &cfg);

    /* Parse baudrate */
    cfg.baudrate = atoi(argv[1]);

    /* Parse optional data bits */
    if (argc > 2) {
        cfg.data_bits = atoi(argv[2]);
    }

    /* Parse optional parity */
    if (argc > 3) {
        if (strcmp(argv[3], "none") == 0) {
            cfg.parity = UART_ADV_PARITY_NONE;
        } else if (strcmp(argv[3], "odd") == 0) {
            cfg.parity = UART_ADV_PARITY_ODD;
        } else if (strcmp(argv[3], "even") == 0) {
            cfg.parity = UART_ADV_PARITY_EVEN;
        }
    }

    /* Parse optional stop bits */
    if (argc > 4) {
        if (strcmp(argv[4], "1") == 0) {
            cfg.stop_bits = UART_ADV_STOPBITS_1;
        } else if (strcmp(argv[4], "2") == 0) {
            cfg.stop_bits = UART_ADV_STOPBITS_2;
        }
    }

    ret = uart_adv_configure(dev, &cfg);
    if (ret < 0) {
        shell_error(sh, "Configuration failed: %d", ret);
        return ret;
    }

    shell_print(sh, "Configured: %u baud, %u data bits",
                cfg.baudrate, cfg.data_bits);
    return 0;
}

static int cmd_uart_stats_reset(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_uart_adv_dev();

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "UART device not available");
        return -ENODEV;
    }

    uart_adv_stats_reset(dev);
    shell_print(sh, "Statistics reset");

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(uart_adv_cmds,
    SHELL_CMD(status, NULL, "Show UART status and statistics", cmd_uart_status),
    SHELL_CMD(send, NULL, "Send text: uart send <text>", cmd_uart_send),
    SHELL_CMD(recv, NULL, "Receive data: uart recv [timeout_ms]", cmd_uart_recv),
    SHELL_CMD(loopback, NULL, "Run loopback test", cmd_uart_loopback),
    SHELL_CMD(config, NULL, "Configure UART: config <baud> [bits] [parity] [stop]",
              cmd_uart_config),
    SHELL_CMD(reset, NULL, "Reset statistics", cmd_uart_stats_reset),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(uart, &uart_adv_cmds, "UART Advanced Driver commands", NULL);
