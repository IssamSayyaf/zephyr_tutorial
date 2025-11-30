# Lesson 6: Testing and Debugging Drivers

## Overview

This lesson covers testing strategies and debugging techniques for Zephyr drivers.

---

## Testing Strategies

### 1. Unit Testing with Ztest

```c
/* tests/src/test_my_driver.c */

#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include "my_driver.h"

static const struct device *test_dev;

static void *test_setup(void)
{
    test_dev = DEVICE_DT_GET(DT_NODELABEL(my_device));
    zassert_true(device_is_ready(test_dev), "Device not ready");
    return NULL;
}

ZTEST_SUITE(my_driver_tests, NULL, test_setup, NULL, NULL, NULL);

ZTEST(my_driver_tests, test_init)
{
    zassert_true(device_is_ready(test_dev), "Device should be ready");
}

ZTEST(my_driver_tests, test_write_read)
{
    uint8_t tx_data[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t rx_data[4] = {0};
    int ret;

    ret = my_driver_write(test_dev, tx_data, sizeof(tx_data));
    zassert_equal(ret, sizeof(tx_data), "Write failed");

    ret = my_driver_read(test_dev, rx_data, sizeof(rx_data));
    zassert_true(ret > 0, "Read failed");
    zassert_mem_equal(tx_data, rx_data, sizeof(tx_data), "Data mismatch");
}

ZTEST(my_driver_tests, test_invalid_args)
{
    int ret;

    ret = my_driver_read(test_dev, NULL, 10);
    zassert_equal(ret, -EINVAL, "Should reject NULL buffer");

    ret = my_driver_write(test_dev, NULL, 10);
    zassert_equal(ret, -EINVAL, "Should reject NULL buffer");
}

ZTEST(my_driver_tests, test_configure)
{
    struct my_driver_config cfg = {
        .param1 = 100,
        .param2 = 200,
    };

    int ret = my_driver_configure(test_dev, &cfg);
    zassert_equal(ret, 0, "Configure failed");
}
```

### Test CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(my_driver_tests)

target_sources(app PRIVATE
    src/test_my_driver.c
)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
```

### Test prj.conf

```kconfig
CONFIG_ZTEST=y
CONFIG_ZTEST_NEW_API=y
CONFIG_MY_DRIVER=y
CONFIG_LOG=y
CONFIG_MY_DRIVER_LOG_LEVEL_DBG=y
```

---

## 2. Integration Testing

### Test Application

```c
/* tests/integration/src/main.c */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include "my_driver.h"

LOG_MODULE_REGISTER(integration_test, LOG_LEVEL_INF);

#define TEST_ITERATIONS 100

int main(void)
{
    const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(my_device));
    int pass_count = 0;
    int fail_count = 0;

    if (!device_is_ready(dev)) {
        LOG_ERR("Device not ready");
        return -1;
    }

    LOG_INF("Starting integration tests (%d iterations)", TEST_ITERATIONS);

    for (int i = 0; i < TEST_ITERATIONS; i++) {
        uint8_t tx_data[32];
        uint8_t rx_data[32];

        /* Generate test data */
        for (int j = 0; j < sizeof(tx_data); j++) {
            tx_data[j] = (i + j) & 0xFF;
        }

        /* Write */
        int ret = my_driver_write(dev, tx_data, sizeof(tx_data));
        if (ret != sizeof(tx_data)) {
            LOG_ERR("Write failed at iteration %d: %d", i, ret);
            fail_count++;
            continue;
        }

        /* Small delay */
        k_msleep(10);

        /* Read back */
        ret = my_driver_read(dev, rx_data, sizeof(rx_data));
        if (ret != sizeof(rx_data)) {
            LOG_ERR("Read failed at iteration %d: %d", i, ret);
            fail_count++;
            continue;
        }

        /* Verify */
        if (memcmp(tx_data, rx_data, sizeof(tx_data)) != 0) {
            LOG_ERR("Data mismatch at iteration %d", i);
            fail_count++;
            continue;
        }

        pass_count++;

        if ((i + 1) % 10 == 0) {
            LOG_INF("Progress: %d/%d", i + 1, TEST_ITERATIONS);
        }
    }

    LOG_INF("=== Test Results ===");
    LOG_INF("Passed: %d", pass_count);
    LOG_INF("Failed: %d", fail_count);
    LOG_INF("Pass rate: %d%%", (pass_count * 100) / TEST_ITERATIONS);

    return (fail_count == 0) ? 0 : -1;
}
```

---

## Debugging Techniques

### 1. Logging

```c
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(my_driver, LOG_LEVEL_DBG);

void my_function(void)
{
    LOG_ERR("Error: %d", error_code);
    LOG_WRN("Warning message");
    LOG_INF("Info: value=%d", value);
    LOG_DBG("Debug: detailed info");

    /* Hexdump for binary data */
    LOG_HEXDUMP_DBG(buffer, len, "Buffer contents:");
}
```

### Enable Debug Logging

```kconfig
# prj.conf
CONFIG_LOG=y
CONFIG_LOG_MODE_IMMEDIATE=y
CONFIG_MY_DRIVER_LOG_LEVEL_DBG=y
```

### 2. Assertions

```c
#include <zephyr/sys/__assert.h>

void critical_function(void *ptr, size_t len)
{
    /* Will halt if condition false (debug builds) */
    __ASSERT(ptr != NULL, "Pointer must not be NULL");
    __ASSERT(len > 0 && len <= MAX_LEN, "Invalid length: %zu", len);

    /* No-op in release builds */
    __ASSERT_NO_MSG(device_is_ready(dev));
}
```

Enable assertions:
```kconfig
CONFIG_ASSERT=y
CONFIG_ASSERT_LEVEL=2
```

### 3. Stack Analysis

```kconfig
# Enable stack analysis
CONFIG_THREAD_ANALYZER=y
CONFIG_THREAD_ANALYZER_USE_PRINTK=y
CONFIG_THREAD_ANALYZER_AUTO=y
CONFIG_THREAD_ANALYZER_AUTO_INTERVAL=10
CONFIG_THREAD_NAME=y
```

### 4. Shell Commands for Debugging

```c
#include <zephyr/shell/shell.h>

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(my_device));
    struct my_driver_data *data = dev->data;

    shell_print(sh, "Device: %s", dev->name);
    shell_print(sh, "  Initialized: %s", data->initialized ? "yes" : "no");
    shell_print(sh, "  RX count: %u", data->rx_count);
    shell_print(sh, "  TX count: %u", data->tx_count);
    shell_print(sh, "  Error count: %u", data->error_count);

    return 0;
}

static int cmd_dump_regs(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(my_device));
    const struct my_driver_config *config = dev->config;

    shell_print(sh, "Register dump:");

    for (int i = 0; i < 16; i++) {
        uint32_t val = sys_read32(config->base_addr + (i * 4));
        shell_print(sh, "  [0x%02x] = 0x%08x", i * 4, val);
    }

    return 0;
}

static int cmd_test(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(my_device));
    uint8_t test_data[] = {0xAA, 0x55, 0xAA, 0x55};
    int ret;

    shell_print(sh, "Running loopback test...");

    ret = my_driver_write(dev, test_data, sizeof(test_data));
    if (ret < 0) {
        shell_error(sh, "Write failed: %d", ret);
        return ret;
    }

    uint8_t rx_data[4];
    ret = my_driver_read(dev, rx_data, sizeof(rx_data));
    if (ret < 0) {
        shell_error(sh, "Read failed: %d", ret);
        return ret;
    }

    if (memcmp(test_data, rx_data, sizeof(test_data)) == 0) {
        shell_print(sh, "Test PASSED");
    } else {
        shell_error(sh, "Test FAILED - data mismatch");
        shell_hexdump(sh, rx_data, sizeof(rx_data));
    }

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(my_driver_shell,
    SHELL_CMD(status, NULL, "Show driver status", cmd_status),
    SHELL_CMD(dump, NULL, "Dump registers", cmd_dump_regs),
    SHELL_CMD(test, NULL, "Run loopback test", cmd_test),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(mydrv, &my_driver_shell, "My driver commands", NULL);
```

---

## Hardware Debugging

### 1. J-Link/SWD Debugging

```bash
# Start debug server
west debug

# Or with specific runner
west debug --runner jlink
```

### GDB Commands

```gdb
# Breakpoints
break my_driver_init
break my_driver.c:150

# Watchpoints
watch data->rx_count
rwatch config->base_addr

# Print variables
print *data
print/x config->base_addr

# Memory examination
x/16xw 0x40000000

# Backtrace
bt

# Step through code
step
next
continue
```

### 2. RTT Logging (Segger)

```kconfig
CONFIG_LOG=y
CONFIG_LOG_BACKEND_RTT=y
CONFIG_USE_SEGGER_RTT=y
CONFIG_RTT_CONSOLE=y
```

### 3. Logic Analyzer Integration

```c
/* Toggle GPIO for timing analysis */
struct gpio_dt_spec debug_gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(debug_pin), gpios);

void timed_operation(void)
{
    gpio_pin_set_dt(&debug_gpio, 1);  /* Start marker */

    /* Operation to measure */
    do_operation();

    gpio_pin_set_dt(&debug_gpio, 0);  /* End marker */
}
```

---

## Performance Profiling

### Timing Measurements

```c
#include <zephyr/timing/timing.h>

void measure_performance(void)
{
    timing_t start, end;
    uint64_t cycles;

    timing_init();
    timing_start();

    start = timing_counter_get();

    /* Operation to measure */
    my_driver_write(dev, data, len);

    end = timing_counter_get();

    cycles = timing_cycles_get(&start, &end);
    uint64_t ns = timing_cycles_to_ns(cycles);

    LOG_INF("Operation took %llu ns (%llu cycles)", ns, cycles);
}
```

### Enable Timing

```kconfig
CONFIG_TIMING_FUNCTIONS=y
CONFIG_TIMING_FUNCTIONS_NEED_AT_BOOT=y
```

---

## Common Issues and Solutions

### Issue: Device Not Ready

```c
const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(my_device));
if (!device_is_ready(dev)) {
    /* Check:
     * 1. Device tree node has status = "okay"
     * 2. Driver is enabled in Kconfig
     * 3. Init priority is correct
     * 4. Dependencies are met
     */
}
```

### Issue: Interrupt Not Firing

```c
/* Checklist:
 * 1. IRQ connected with IRQ_CONNECT
 * 2. IRQ enabled with irq_enable()
 * 3. Device interrupt enabled
 * 4. Interrupt handler implemented correctly
 * 5. Interrupt cleared in handler
 */

static void check_interrupt_setup(void)
{
    LOG_INF("IRQ number: %d", DT_IRQN(MY_NODE));
    LOG_INF("IRQ enabled: %d", irq_is_enabled(DT_IRQN(MY_NODE)));
}
```

### Issue: Data Corruption

```c
/* Debugging data corruption */
void debug_transfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    LOG_HEXDUMP_DBG(tx, len, "TX data:");

    int ret = perform_transfer(tx, rx, len);

    LOG_HEXDUMP_DBG(rx, len, "RX data:");

    /* Check for common patterns */
    bool all_zero = true;
    bool all_ff = true;

    for (size_t i = 0; i < len; i++) {
        if (rx[i] != 0x00) all_zero = false;
        if (rx[i] != 0xFF) all_ff = false;
    }

    if (all_zero) {
        LOG_WRN("All zeros - device may not be responding");
    }
    if (all_ff) {
        LOG_WRN("All 0xFF - possible bus issue");
    }
}
```

---

## Test Automation

### Twister (Zephyr Test Runner)

```bash
# Run tests
./scripts/twister -T tests/drivers/my_driver

# With specific board
./scripts/twister -T tests/drivers/my_driver -p nrf52840dk_nrf52840

# Generate coverage report
./scripts/twister -T tests/drivers/my_driver --coverage
```

### testcase.yaml

```yaml
tests:
  drivers.my_driver.unit:
    tags: drivers
    platform_allow: native_sim nrf52840dk/nrf52840
    integration_platforms:
      - native_sim

  drivers.my_driver.integration:
    tags: drivers slow
    platform_allow: nrf52840dk/nrf52840
    harness: console
    harness_config:
      type: one_line
      regex:
        - "Test PASSED"
```

---

## Summary

### Testing Checklist

- [ ] Unit tests with Ztest
- [ ] Integration tests on real hardware
- [ ] Error condition testing
- [ ] Performance benchmarks
- [ ] Stress testing (long-running)
- [ ] Edge cases (buffer boundaries, timeouts)

### Debugging Checklist

- [ ] Enable debug logging
- [ ] Add shell commands for inspection
- [ ] Use assertions for invariants
- [ ] Monitor stack usage
- [ ] Profile timing-critical paths
- [ ] Use hardware debugger when needed

---

## Next Steps

Now that you understand testing and debugging, proceed to implement the actual drivers in the `modules/custom_drivers/` directory.
