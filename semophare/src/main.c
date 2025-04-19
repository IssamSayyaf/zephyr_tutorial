#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/__assert.h>
#include <string.h>


#define STACKSIZE 1024
#define PRIORITY 7
#define SLEEPTIME 500

void helloLoop (const char *my_name, struct k_sem *my_sem,
                struct k_sem *other_sem) {
    const char *tname;
    uint8_t cpu;
    struct k_thread *current_thread;
    while (true) {
        k_sem_take(my_sem, K_FOREVER);
        current_thread = k_current_get();
        tname = k_thread_name_get(current_thread);
        cpu = 0;
        if (tname == NULL) {
            printk("%s: Hello World from cpu %d on %s!\n",
                   my_name, cpu, CONFIG_BOARD);
        } else {
            printk("%s: Hello World from cpu %d on %s!\n",
                   tname, cpu, CONFIG_BOARD);
        }
        k_busy_wait(100000);
        k_sem_give(other_sem);
        k_msleep(SLEEPTIME);
    }
}

K_THREAD_STACK_DEFINE(threadA_stack_area, STACKSIZE);
static struct k_thread threadA_data;

K_THREAD_STACK_DEFINE(threadB_stack_area, STACKSIZE);
static struct k_thread threadB_data;



K_SEM_DEFINE(threadA_sem, 1, 1);  /* K_SEM_DEFINE(name, initial_count, maximum_count)
                                     name: Name of the semaphore
                                     initial_count: Initial count value (1 means available)
                                     maximum_count: Maximum count value (1 makes it binary) */
K_SEM_DEFINE(threadB_sem, 0, 1);  /* binary semaphore starts off "unavailable" */

#define ARG_UNUSED(x) (void)(x)

void threadA(void *dummy1, void *dummy2, void *dummy3) {
    ARG_UNUSED(dummy1);
    ARG_UNUSED(dummy2);
    ARG_UNUSED(dummy3);
    /* invoke routine to ping-pong hello messages with threadB */
    helloLoop(__func__, &threadA_sem, &threadB_sem);
}

void threadB(void *dummy1, void *dummy2, void *dummy3) {
    ARG_UNUSED(dummy1);
    ARG_UNUSED(dummy2);
    ARG_UNUSED(dummy3);
    /* invoke routine to ping-pong hello messages with threadA */
    helloLoop(__func__, &threadB_sem, &threadA_sem);
}

void main(void) {
    k_thread_create(&threadA_data, threadA_stack_area,
                    K_THREAD_STACK_SIZEOF(threadA_stack_area),
                    threadA, NULL, NULL, NULL,
                    PRIORITY, 0, K_FOREVER);
    k_thread_name_set(&threadA_data, "thread_a");
    
    k_thread_create(&threadB_data, threadB_stack_area,
                    K_THREAD_STACK_SIZEOF(threadB_stack_area),
                    threadB, NULL, NULL, NULL,
                    PRIORITY, 0, K_FOREVER);
    k_thread_name_set(&threadB_data, "thread_b");
    
    k_thread_start(&threadA_data);
    k_thread_start(&threadB_data);
}