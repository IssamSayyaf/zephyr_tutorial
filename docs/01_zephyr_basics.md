# Lesson 1: Zephyr RTOS Basics

## Overview

Zephyr is a scalable real-time operating system (RTOS) supporting multiple hardware architectures, optimized for resource-constrained devices, and built with security in mind.

---

## Key Concepts

### 1. Kernel Objects

Zephyr provides fundamental kernel objects for building applications:

| Object | Purpose |
|--------|---------|
| **Threads** | Independent execution contexts |
| **Semaphores** | Synchronization primitives |
| **Mutexes** | Mutual exclusion |
| **Message Queues** | Thread-safe data passing |
| **Work Queues** | Deferred work execution |
| **Timers** | Time-based callbacks |

### 2. Build System

Zephyr uses CMake and a tool called `west` for building:

```bash
# Build command structure
west build -b <board> [source_dir] [-- cmake_options]

# Examples
west build -b nrf52840dk/nrf52840
west build -b stm32f4_disco -p always -- -DCONFIG_DEBUG=y
```

### 3. Configuration System

Configuration is managed through multiple mechanisms:

```
┌─────────────────┐
│   prj.conf      │  ← Application configuration
├─────────────────┤
│   Kconfig       │  ← Configuration definitions
├─────────────────┤
│   .overlay      │  ← Device tree overlays
└─────────────────┘
```

---

## Application Structure

### Minimal Application

```
my_app/
├── CMakeLists.txt      # Build configuration
├── prj.conf            # Project configuration
├── src/
│   └── main.c          # Application code
└── boards/             # Board-specific overlays (optional)
    └── nrf52840dk_nrf52840.overlay
```

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(my_app)

target_sources(app PRIVATE src/main.c)
```

### prj.conf

```kconfig
# Enable logging
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=3

# Enable GPIO
CONFIG_GPIO=y

# Enable serial console
CONFIG_SERIAL=y
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
```

### main.c

```c
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
    LOG_INF("Zephyr Application Started!");

    while (1) {
        LOG_INF("Hello from Zephyr!");
        k_sleep(K_SECONDS(1));
    }

    return 0;
}
```

---

## Threading Model

### Creating Threads

```c
#include <zephyr/kernel.h>

#define STACK_SIZE 1024
#define PRIORITY 5

K_THREAD_STACK_DEFINE(my_stack, STACK_SIZE);
struct k_thread my_thread_data;

void my_thread_entry(void *p1, void *p2, void *p3)
{
    while (1) {
        /* Thread work */
        k_sleep(K_MSEC(100));
    }
}

int main(void)
{
    k_tid_t tid = k_thread_create(&my_thread_data, my_stack,
                                  K_THREAD_STACK_SIZEOF(my_stack),
                                  my_thread_entry,
                                  NULL, NULL, NULL,
                                  PRIORITY, 0, K_NO_WAIT);
    return 0;
}
```

### Thread Priority

- Lower number = Higher priority
- Priority 0 is highest (reserved for system)
- Cooperative threads: negative priority
- Preemptive threads: positive priority

---

## Synchronization Primitives

### Semaphore

```c
K_SEM_DEFINE(my_sem, 0, 1);  /* Initial=0, Max=1 */

/* Thread 1: Wait for semaphore */
k_sem_take(&my_sem, K_FOREVER);

/* Thread 2: Signal semaphore */
k_sem_give(&my_sem);
```

### Mutex

```c
K_MUTEX_DEFINE(my_mutex);

/* Critical section */
k_mutex_lock(&my_mutex, K_FOREVER);
/* Protected code */
k_mutex_unlock(&my_mutex);
```

### Message Queue

```c
K_MSGQ_DEFINE(my_msgq, sizeof(struct my_data), 10, 4);

/* Send */
struct my_data data = {...};
k_msgq_put(&my_msgq, &data, K_NO_WAIT);

/* Receive */
struct my_data rx_data;
k_msgq_get(&my_msgq, &rx_data, K_FOREVER);
```

---

## Timing and Delays

### Delay Functions

```c
/* Sleep for duration */
k_sleep(K_MSEC(100));
k_sleep(K_SECONDS(1));
k_sleep(K_MINUTES(5));

/* Busy wait (not recommended for long delays) */
k_busy_wait(1000);  /* microseconds */
```

### Timers

```c
void timer_expiry(struct k_timer *timer)
{
    /* Timer expired - runs in ISR context */
}

K_TIMER_DEFINE(my_timer, timer_expiry, NULL);

/* Start timer: period, initial delay */
k_timer_start(&my_timer, K_MSEC(1000), K_MSEC(1000));

/* Stop timer */
k_timer_stop(&my_timer);
```

---

## Logging System

### Log Levels

| Level | Macro | Purpose |
|-------|-------|---------|
| 0 | `LOG_LEVEL_NONE` | No logging |
| 1 | `LOG_LEVEL_ERR` | Errors only |
| 2 | `LOG_LEVEL_WRN` | Warnings and errors |
| 3 | `LOG_LEVEL_INF` | Info, warnings, errors |
| 4 | `LOG_LEVEL_DBG` | All messages |

### Usage

```c
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(my_module, LOG_LEVEL_DBG);

void example(void)
{
    LOG_ERR("Error: %d", error_code);
    LOG_WRN("Warning message");
    LOG_INF("Info: value=%d", value);
    LOG_DBG("Debug: ptr=%p", ptr);

    /* Hexdump */
    LOG_HEXDUMP_INF(buffer, sizeof(buffer), "Buffer:");
}
```

---

## Memory Management

### Static Allocation (Preferred)

```c
/* Stack-allocated */
uint8_t buffer[256];

/* Statically defined kernel objects */
K_SEM_DEFINE(my_sem, 0, 1);
K_MUTEX_DEFINE(my_mutex);
```

### Dynamic Allocation

```c
/* Enable in prj.conf: CONFIG_HEAP_MEM_POOL_SIZE=1024 */

void *ptr = k_malloc(size);
if (ptr == NULL) {
    LOG_ERR("Allocation failed");
}

k_free(ptr);
```

---

## Exercise

Create a simple application that:

1. Creates two threads
2. Thread 1 increments a counter every 500ms
3. Thread 2 prints the counter value every 1 second
4. Use a mutex to protect the shared counter

### Solution

```c
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(exercise, LOG_LEVEL_INF);

#define STACK_SIZE 1024
#define PRODUCER_PRIORITY 5
#define CONSUMER_PRIORITY 6

K_THREAD_STACK_DEFINE(producer_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(consumer_stack, STACK_SIZE);

struct k_thread producer_thread;
struct k_thread consumer_thread;

K_MUTEX_DEFINE(counter_mutex);
static uint32_t counter = 0;

void producer_entry(void *p1, void *p2, void *p3)
{
    while (1) {
        k_mutex_lock(&counter_mutex, K_FOREVER);
        counter++;
        k_mutex_unlock(&counter_mutex);
        k_sleep(K_MSEC(500));
    }
}

void consumer_entry(void *p1, void *p2, void *p3)
{
    while (1) {
        k_mutex_lock(&counter_mutex, K_FOREVER);
        LOG_INF("Counter value: %u", counter);
        k_mutex_unlock(&counter_mutex);
        k_sleep(K_SECONDS(1));
    }
}

int main(void)
{
    LOG_INF("Starting exercise application");

    k_thread_create(&producer_thread, producer_stack,
                    K_THREAD_STACK_SIZEOF(producer_stack),
                    producer_entry, NULL, NULL, NULL,
                    PRODUCER_PRIORITY, 0, K_NO_WAIT);

    k_thread_create(&consumer_thread, consumer_stack,
                    K_THREAD_STACK_SIZEOF(consumer_stack),
                    consumer_entry, NULL, NULL, NULL,
                    CONSUMER_PRIORITY, 0, K_NO_WAIT);

    return 0;
}
```

---

## Next Steps

Continue to [Lesson 2: Device Model](02_device_model.md) to learn about Zephyr's device model and driver architecture.
