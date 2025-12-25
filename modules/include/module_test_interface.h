/*
 * module_test_interface.h
 *
 * Public interface for accessing module test variables and functions
 * Use this header in your debugger or host application to monitor module state
 */

#ifndef MODULE_TEST_INTERFACE_H
#define MODULE_TEST_INTERFACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Test status bit definitions */
#define TEST_TASK1_OK       (1 << 0)
#define TEST_TASK2_OK       (1 << 1)
#define TEST_TASK3_OK       (1 << 2)
#define TEST_TIMER_OK       (1 << 3)
#define TEST_SEMAPHORE_OK   (1 << 4)
#define TEST_QUEUE_OK       (1 << 5)
#define TEST_DELAY_OK       (1 << 6)
#define TEST_ALL_OK         0x7F

/* Module test variables (extern declarations for host to access) */
extern volatile uint32_t cnt;
extern volatile uint32_t task1_counter;
extern volatile uint32_t task2_counter;
extern volatile uint32_t task3_counter;
extern volatile uint32_t timer_fired;
extern volatile uint32_t semaphore_signals;
extern volatile uint32_t queue_messages;
extern volatile uint32_t test_status;

/* Test task functions (can be started from host if needed) */
extern void test_task1(void* args);
extern void test_task2(void* args);
extern void test_task3(void* args);

/* Timer callback */
extern void timer_callback(void* args);

/* Module entry point */
extern int module_entry(uint32_t reason, void* ctx);

/* Helper macros for test status checking */
#define IS_TEST_PASSED(status, test_bit)  (((status) & (test_bit)) != 0)
#define IS_ALL_TESTS_PASSED(status)       (((status) & TEST_ALL_OK) == TEST_ALL_OK)

/* Helper function to get human-readable test status (implement in host if needed) */
static inline const char* get_test_status_string(uint32_t status) {
    if (IS_ALL_TESTS_PASSED(status)) {
        return "ALL TESTS PASSED";
    }
    return "TESTS IN PROGRESS";
}

#ifdef __cplusplus
}
#endif

#endif /* MODULE_TEST_INTERFACE_H */
