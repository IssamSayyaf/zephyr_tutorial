/**
 * @file main.c
 * @brief SPI Advanced Driver Example
 *
 * Demonstrates SPI communication with multiple devices.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include "spi_advanced.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#if DT_NODE_EXISTS(DT_NODELABEL(spi_adv0))
#define SPI_ADV_NODE DT_NODELABEL(spi_adv0)
#else
#error "No spi_adv0 node found in devicetree"
#endif

#define TEST_DEVICE_ID 0

int main(void)
{
    const struct device *dev = DEVICE_DT_GET(SPI_ADV_NODE);
    int ret;

    LOG_INF("SPI Advanced Driver Example");
    LOG_INF("===========================");

    if (!device_is_ready(dev)) {
        LOG_ERR("SPI device not ready");
        return -ENODEV;
    }

    /* Configure SPI device 0 */
    struct spi_adv_device_config cfg = {
        .frequency = 1000000,  /* 1 MHz */
        .mode = SPI_ADV_MODE_0,
        .bit_order = SPI_ADV_MSB_FIRST,
        .word_size = 8,
        .cs_gpio = {0},  /* No CS GPIO in this example */
        .cs_ctrl = SPI_ADV_CS_ACTIVE_LOW,
        .cs_delay_us = 0,
    };

    ret = spi_adv_device_configure(dev, TEST_DEVICE_ID, &cfg);
    if (ret < 0) {
        LOG_ERR("Device configuration failed: %d", ret);
        return ret;
    }

    LOG_INF("SPI device %d configured: %u Hz, Mode %d",
            TEST_DEVICE_ID, cfg.frequency, cfg.mode);

    /* Demo: Write some data */
    LOG_INF("");
    LOG_INF("=== SPI Write Demo ===");

    uint8_t tx_data[] = {0xAA, 0x55, 0x01, 0x02, 0x03, 0x04};
    ret = spi_adv_write(dev, TEST_DEVICE_ID, tx_data, sizeof(tx_data));
    if (ret < 0) {
        LOG_ERR("Write failed: %d", ret);
    } else {
        LOG_INF("Wrote %d bytes", ret);
        LOG_HEXDUMP_INF(tx_data, sizeof(tx_data), "TX:");
    }

    /* Demo: Full-duplex transfer */
    LOG_INF("");
    LOG_INF("=== SPI Full-Duplex Demo ===");

    uint8_t rx_data[6] = {0};
    ret = spi_adv_transceive(dev, TEST_DEVICE_ID, tx_data, rx_data, sizeof(tx_data));
    if (ret < 0) {
        LOG_ERR("Transceive failed: %d", ret);
    } else {
        LOG_INF("Transferred %d bytes", ret);
        LOG_HEXDUMP_INF(rx_data, sizeof(rx_data), "RX:");
    }

    /* Demo: Register access (simulated) */
    LOG_INF("");
    LOG_INF("=== SPI Register Access Demo ===");

    uint8_t reg_val;
    ret = spi_adv_reg_read_byte(dev, TEST_DEVICE_ID, 0x00, &reg_val);
    LOG_INF("Reg 0x00 = 0x%02x (ret=%d)", reg_val, ret);

    ret = spi_adv_reg_write_byte(dev, TEST_DEVICE_ID, 0x01, 0x42);
    LOG_INF("Wrote 0x42 to reg 0x01 (ret=%d)", ret);

    /* Show statistics */
    LOG_INF("");
    LOG_INF("=== Statistics ===");

    struct spi_adv_stats stats;
    spi_adv_stats_get(dev, &stats);

    LOG_INF("TX bytes:      %u", stats.tx_bytes);
    LOG_INF("RX bytes:      %u", stats.rx_bytes);
    LOG_INF("Transfers:     %u", stats.transfers);
    LOG_INF("Errors:        %u", stats.errors);

    /* Demo different SPI modes */
    LOG_INF("");
    LOG_INF("=== SPI Mode Changes ===");

    for (int mode = 0; mode <= 3; mode++) {
        spi_adv_set_mode(dev, TEST_DEVICE_ID, mode);
        LOG_INF("Switched to Mode %d", mode);

        /* Do a quick transfer */
        uint8_t dummy[2] = {0xFF, 0xFF};
        spi_adv_write(dev, TEST_DEVICE_ID, dummy, 2);
    }

    /* Demo frequency changes */
    LOG_INF("");
    LOG_INF("=== SPI Frequency Changes ===");

    uint32_t frequencies[] = {100000, 500000, 1000000, 4000000};
    for (int i = 0; i < ARRAY_SIZE(frequencies); i++) {
        spi_adv_set_frequency(dev, TEST_DEVICE_ID, frequencies[i]);
        LOG_INF("Frequency: %u Hz", frequencies[i]);

        uint8_t dummy[4] = {0x01, 0x02, 0x03, 0x04};
        spi_adv_write(dev, TEST_DEVICE_ID, dummy, 4);
    }

    LOG_INF("");
    LOG_INF("Example complete. Use shell commands for more testing.");

    /* Keep running for shell access */
    while (1) {
        k_sleep(K_SECONDS(10));
    }

    return 0;
}
