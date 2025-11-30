/**
 * @file spi_advanced_shell.c
 * @brief Shell commands for SPI Advanced Driver
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/device.h>
#include <stdlib.h>
#include <string.h>

#include "spi_advanced.h"

#define SPI_ADV_DEVICE DT_NODELABEL(spi_adv0)

static const struct device *get_spi_adv_dev(void)
{
#if DT_NODE_EXISTS(SPI_ADV_DEVICE)
    return DEVICE_DT_GET(SPI_ADV_DEVICE);
#else
    return NULL;
#endif
}

static int cmd_spi_status(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_spi_adv_dev();
#ifdef CONFIG_SPI_ADVANCED_STATS
    struct spi_adv_stats stats;
#endif

    if (dev == NULL) {
        shell_error(sh, "SPI device not found");
        return -ENODEV;
    }

    if (!device_is_ready(dev)) {
        shell_error(sh, "SPI device not ready");
        return -ENODEV;
    }

    shell_print(sh, "SPI Advanced Driver Status");
    shell_print(sh, "==========================");

    shell_print(sh, "Configured devices:");
    for (int i = 0; i < 8; i++) {
        if (spi_adv_device_is_configured(dev, i)) {
            struct spi_adv_device_config cfg;
            spi_adv_device_config_get(dev, i, &cfg);
            shell_print(sh, "  Device %d: %u Hz, mode %d, %u bits",
                        i, cfg.frequency, cfg.mode, cfg.word_size);
        }
    }

#ifdef CONFIG_SPI_ADVANCED_STATS
    spi_adv_stats_get(dev, &stats);
    shell_print(sh, "");
    shell_print(sh, "Statistics:");
    shell_print(sh, "  TX bytes:      %u", stats.tx_bytes);
    shell_print(sh, "  RX bytes:      %u", stats.rx_bytes);
    shell_print(sh, "  Transfers:     %u", stats.transfers);
    shell_print(sh, "  DMA transfers: %u", stats.dma_transfers);
    shell_print(sh, "  Errors:        %u", stats.errors);
    shell_print(sh, "  Last xfer:     %u us", stats.last_transfer_us);
    shell_print(sh, "  Max xfer:      %u us", stats.max_transfer_us);
#endif

    return 0;
}

static int cmd_spi_config(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_spi_adv_dev();
    uint8_t device_id;
    uint32_t frequency;
    int mode = 0;
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "SPI device not available");
        return -ENODEV;
    }

    if (argc < 3) {
        shell_print(sh, "Usage: spi config <device_id> <frequency> [mode]");
        shell_print(sh, "  device_id: 0-7");
        shell_print(sh, "  frequency: in Hz");
        shell_print(sh, "  mode: 0-3 (default 0)");
        return -EINVAL;
    }

    device_id = atoi(argv[1]);
    frequency = atoi(argv[2]);

    if (argc > 3) {
        mode = atoi(argv[3]);
    }

    struct spi_adv_device_config cfg = {
        .frequency = frequency,
        .mode = mode,
        .bit_order = SPI_ADV_MSB_FIRST,
        .word_size = 8,
        .cs_gpio = {0},
        .cs_ctrl = SPI_ADV_CS_ACTIVE_LOW,
        .cs_delay_us = 0,
    };

    ret = spi_adv_device_configure(dev, device_id, &cfg);
    if (ret < 0) {
        shell_error(sh, "Configuration failed: %d", ret);
        return ret;
    }

    shell_print(sh, "Device %u configured: %u Hz, mode %d",
                device_id, frequency, mode);
    return 0;
}

static int cmd_spi_write(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_spi_adv_dev();
    uint8_t device_id;
    uint8_t data[32];
    size_t len = 0;
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "SPI device not available");
        return -ENODEV;
    }

    if (argc < 3) {
        shell_print(sh, "Usage: spi write <device_id> <byte1> [byte2] ...");
        return -EINVAL;
    }

    device_id = atoi(argv[1]);

    for (int i = 2; i < argc && len < sizeof(data); i++) {
        data[len++] = (uint8_t)strtol(argv[i], NULL, 16);
    }

    shell_print(sh, "Writing %zu bytes to device %u", len, device_id);
    shell_hexdump(sh, data, len);

    ret = spi_adv_write(dev, device_id, data, len);
    if (ret < 0) {
        shell_error(sh, "Write failed: %d", ret);
        return ret;
    }

    shell_print(sh, "Write successful");
    return 0;
}

static int cmd_spi_read(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_spi_adv_dev();
    uint8_t device_id;
    size_t len;
    uint8_t data[64];
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "SPI device not available");
        return -ENODEV;
    }

    if (argc < 3) {
        shell_print(sh, "Usage: spi read <device_id> <length>");
        return -EINVAL;
    }

    device_id = atoi(argv[1]);
    len = atoi(argv[2]);

    if (len > sizeof(data)) {
        len = sizeof(data);
    }

    shell_print(sh, "Reading %zu bytes from device %u", len, device_id);

    ret = spi_adv_read(dev, device_id, data, len);
    if (ret < 0) {
        shell_error(sh, "Read failed: %d", ret);
        return ret;
    }

    shell_print(sh, "Read %d bytes:", ret);
    shell_hexdump(sh, data, ret);

    return 0;
}

static int cmd_spi_xfer(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_spi_adv_dev();
    uint8_t device_id;
    uint8_t tx_data[32];
    uint8_t rx_data[32];
    size_t len = 0;
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "SPI device not available");
        return -ENODEV;
    }

    if (argc < 3) {
        shell_print(sh, "Usage: spi xfer <device_id> <tx_byte1> [tx_byte2] ...");
        return -EINVAL;
    }

    device_id = atoi(argv[1]);

    for (int i = 2; i < argc && len < sizeof(tx_data); i++) {
        tx_data[len++] = (uint8_t)strtol(argv[i], NULL, 16);
    }

    shell_print(sh, "Transceiving %zu bytes with device %u", len, device_id);
    shell_print(sh, "TX:");
    shell_hexdump(sh, tx_data, len);

    ret = spi_adv_transceive(dev, device_id, tx_data, rx_data, len);
    if (ret < 0) {
        shell_error(sh, "Transfer failed: %d", ret);
        return ret;
    }

    shell_print(sh, "RX:");
    shell_hexdump(sh, rx_data, len);

    return 0;
}

static int cmd_spi_reg_read(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_spi_adv_dev();
    uint8_t device_id;
    uint8_t reg_addr;
    size_t len;
    uint8_t data[32];
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "SPI device not available");
        return -ENODEV;
    }

    if (argc < 4) {
        shell_print(sh, "Usage: spi rr <device_id> <reg_addr> <length>");
        return -EINVAL;
    }

    device_id = atoi(argv[1]);
    reg_addr = (uint8_t)strtol(argv[2], NULL, 16);
    len = atoi(argv[3]);

    if (len > sizeof(data)) {
        len = sizeof(data);
    }

    ret = spi_adv_reg_read(dev, device_id, reg_addr, data, len);
    if (ret < 0) {
        shell_error(sh, "Register read failed: %d", ret);
        return ret;
    }

    shell_print(sh, "Reg 0x%02x (%zu bytes):", reg_addr, len);
    shell_hexdump(sh, data, len);

    return 0;
}

static int cmd_spi_reg_write(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_spi_adv_dev();
    uint8_t device_id;
    uint8_t reg_addr;
    uint8_t data[32];
    size_t len = 0;
    int ret;

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "SPI device not available");
        return -ENODEV;
    }

    if (argc < 4) {
        shell_print(sh, "Usage: spi rw <device_id> <reg_addr> <byte1> [byte2] ...");
        return -EINVAL;
    }

    device_id = atoi(argv[1]);
    reg_addr = (uint8_t)strtol(argv[2], NULL, 16);

    for (int i = 3; i < argc && len < sizeof(data); i++) {
        data[len++] = (uint8_t)strtol(argv[i], NULL, 16);
    }

    ret = spi_adv_reg_write(dev, device_id, reg_addr, data, len);
    if (ret < 0) {
        shell_error(sh, "Register write failed: %d", ret);
        return ret;
    }

    shell_print(sh, "Wrote %zu bytes to reg 0x%02x", len, reg_addr);

    return 0;
}

#ifdef CONFIG_SPI_ADVANCED_STATS
static int cmd_spi_stats_reset(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = get_spi_adv_dev();

    if (dev == NULL || !device_is_ready(dev)) {
        shell_error(sh, "SPI device not available");
        return -ENODEV;
    }

    spi_adv_stats_reset(dev);
    shell_print(sh, "Statistics reset");

    return 0;
}
#endif

SHELL_STATIC_SUBCMD_SET_CREATE(spi_adv_cmds,
    SHELL_CMD(status, NULL, "Show SPI status and statistics", cmd_spi_status),
    SHELL_CMD(config, NULL, "Configure device: config <id> <freq> [mode]",
              cmd_spi_config),
    SHELL_CMD(write, NULL, "Write bytes: write <id> <byte1> ...", cmd_spi_write),
    SHELL_CMD(read, NULL, "Read bytes: read <id> <len>", cmd_spi_read),
    SHELL_CMD(xfer, NULL, "Full duplex: xfer <id> <tx1> ...", cmd_spi_xfer),
    SHELL_CMD(rr, NULL, "Reg read: rr <id> <reg> <len>", cmd_spi_reg_read),
    SHELL_CMD(rw, NULL, "Reg write: rw <id> <reg> <byte1> ...", cmd_spi_reg_write),
#ifdef CONFIG_SPI_ADVANCED_STATS
    SHELL_CMD(reset, NULL, "Reset statistics", cmd_spi_stats_reset),
#endif
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(spi, &spi_adv_cmds, "SPI Advanced Driver commands", NULL);
