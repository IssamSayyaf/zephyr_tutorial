/**
 * @file condition_variable_tutorial.c
 * @brief Tutorial code demonstrating condition variable usage in Zephyr RTOS
 *
 * This example illustrates a classic producer-consumer pattern with three threads:
 * - Two producer threads (inc_count) that increment a shared counter
 * - One consumer thread (watch_count) that waits for the counter to reach a threshold
 * 
 * Synchronization is handled using:
 * - A mutex to protect the shared counter
 * - A condition variable to signal when the counter reaches a threshold
 */



#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/__assert.h>


/* Configuration constants */
#define NUM_THREADS 3       /* Total number of threads (2 producers + 1 consumer) */
#define TCOUNT 20           /* Number of times each producer increments counter */
#define COUNT_LIMIT 12      /* Threshold value that triggers the condition */
#define STACK_SIZE 1024     /* Stack size for each thread */

/* Global variables */
static int count = 0;       /* Shared counter that will be protected by mutex */

/* Synchronization primitives */
K_MUTEX_DEFINE(count_mutex);                /* Mutex to protect the shared counter */
K_CONDVAR_DEFINE(count_threshold_cv);       /* Condition variable for signaling */

/* Thread stacks and control blocks */
K_THREAD_STACK_ARRAY_DEFINE(tstacks, NUM_THREADS, STACK_SIZE);
static struct k_thread t[NUM_THREADS];      /* Array to hold thread control blocks */

/**
 * @brief Producer thread function that increments the shared counter
 *
 * This function demonstrates:
 * 1. Acquiring a mutex to protect shared data
 * 2. Signaling a condition variable when a threshold is reached
 * 3. Safe handling of shared resources in a multithreaded environment
 *
 * @param p1 Thread ID passed as void pointer
 * @param p2 Unused parameter
 * @param p3 Unused parameter
 */
void inc_count(void *p1, void *p2, void *p3) {
    int i;
    long my_id = (long)p1;  /* Cast the thread ID back to a long */
    
    /* Increment the counter TCOUNT times */
    for (i = 0; i < TCOUNT; i++) {
        /* Acquire mutex to protect the critical section */
        k_mutex_lock(&count_mutex, K_FOREVER);
        
        /* Critical section - increment the shared counter */
        count++;
        
        /* Check if we've reached the threshold condition */
        /* Note: This check happens while the mutex is locked to ensure atomicity */
        if (count == COUNT_LIMIT) {
            printk("%s: thread %ld, count = %d  Threshold reached.\n",
                   __func__, my_id, count);
            
            /* Signal the condition variable to wake up any waiting threads */
            k_condvar_signal(&count_threshold_cv);
            printk("Just sent signal.\n");
        }
        
        /* Print the current count value */
        printk("%s: thread %ld, count = %d, unlocking mutex\n", 
               __func__, my_id, count);
        
        /* Release the mutex */
        k_mutex_unlock(&count_mutex);
        
        /* Sleep to allow other threads to run */
        k_sleep(K_MSEC(500));  /* Sleep for 500ms */
    }
}

/**
 * @brief Consumer thread function that waits for the counter to reach a threshold
 *
 * This function demonstrates:
 * 1. Waiting on a condition variable
 * 2. Handling the condition once it's triggered
 * 3. Safe coordination between threads using condition variables
 *
 * @param p1 Thread ID passed as void pointer
 * @param p2 Unused parameter
 * @param p3 Unused parameter
 */
void watch_count(void *p1, void *p2, void *p3) {
    long my_id = (long)p1;  /* Cast the thread ID back to a long */
    
    printk("Starting %s: thread %ld\n", __func__, my_id);
    
    /* Lock the mutex before checking the condition */
    k_mutex_lock(&count_mutex, K_FOREVER);
    
    /* Check if the condition is already met */
    while (count < COUNT_LIMIT) {
        printk("%s: thread %ld Count= %d. Going into wait...\n",
               __func__, my_id, count);
        
        /* Wait on the condition variable
         * This atomically:
         * 1. Releases the mutex
         * 2. Blocks the thread until the condition is signaled
         * 3. Re-acquires the mutex after being woken up
         */
        k_condvar_wait(&count_threshold_cv, &count_mutex, K_FOREVER);
        
        /* After wake up, we own the mutex again */
        printk("%s: thread %ld Condition signal received. Count= %d\n",
               __func__, my_id, count);
    }
    
    /* Count has reached the threshold, do something with it */
    printk("%s: thread %ld Updating the value of count...\n", __func__, my_id);
    count += 125;  /* Add a large value to show we've processed the threshold */
    printk("%s: thread %ld count now = %d.\n", __func__, my_id, count);
    
    /* Unlock the mutex */
    printk("%s: thread %ld Unlocking mutex.\n", __func__, my_id);
    k_mutex_unlock(&count_mutex);
}

/**
 * @brief Main function that creates and starts all threads
 *
 * This function demonstrates:
 * 1. Creating threads with different entry points and parameters
 * 2. Waiting for all threads to complete using k_thread_join
 */
int main(void) {
    /* Thread IDs */
    long t1 = 1, t2 = 2, t3 = 3;
    int i;
    
    /* Initialize the counter */
    count = 0;
    /* Priority explanation:
     * K_PRIO_PREEMPT(10): Creates a preemptible thread with priority 10
     *   - Can be interrupted by higher priority threads
     *   - Priority range: 0 (highest) to 15 (lowest)
     *
     * K_PRIO_COOP(10): Creates a cooperative thread with priority 10  
     *   - Only yields control voluntarily
     *   - Won't be preempted by other threads
     *   - Good for critical sections
     *
     * Plain number (10): Same as K_PRIO_PREEMPT(10) by default
     *   - Creates preemptible thread
     *
     * Scheduling delays:
     * K_NO_WAIT: Don't wait at all (return immediately if can't proceed)
     * K_FOREVER: Wait indefinitely until condition is met
     * K_MSEC(x): Wait for specified milliseconds
     */
    /* Create the consumer thread (watch_count) */
    k_thread_create(&t[0], tstacks[0], STACK_SIZE, watch_count,
                   INT_TO_POINTER(t1), NULL, NULL, K_PRIO_PREEMPT(10), /* K_PRIO_PREEMPT(10) means preemptible priority level 10 */
                   0, K_NO_WAIT);
    
    /* Create the first producer thread (inc_count) */
    k_thread_create(&t[1], tstacks[1], STACK_SIZE, inc_count,
                   INT_TO_POINTER(t2), NULL, NULL, K_PRIO_PREEMPT(10),
                   0, K_NO_WAIT);
    
    /* Create the second producer thread (inc_count) */
    k_thread_create(&t[2], tstacks[2], STACK_SIZE, inc_count,
                   INT_TO_POINTER(t3), NULL, NULL, K_PRIO_PREEMPT(10),
                   0, K_NO_WAIT);
    
    /* Wait for all threads to complete */
    for (i = 0; i < NUM_THREADS; i++) {
        /* Wait indefinitely (K_FOREVER) for thread t[i] to complete execution
         * k_thread_join() blocks the current thread until the target thread terminates
         * This ensures all child threads finish before main() continues */
        k_thread_join(&t[i], K_FOREVER);
    }
    
    /* All threads have completed */
    printk("Main(): Waited and joined with %d threads.\n"
           "Final value of count = %d. Done.\n", 
           NUM_THREADS, count);
    return 0;
}

/**
 * Expected output behavior:
 *
 * 1. The watch_count thread starts and waits on the condition
 * 2. The two inc_count threads increment the counter in turns
 * 3. When count reaches COUNT_LIMIT (12), one of the inc_count threads signals
 * 4. The watch_count thread wakes up, adds 125 to count, and exits
 * 5. The inc_count threads continue until they've each done TCOUNT increments
 * 6. Main joins with all threads and prints the final count value
 *
 * This demonstrates proper synchronization between threads using a
 * condition variable with a mutex.
 */