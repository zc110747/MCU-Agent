#ifndef __APP_LOG_H
#define __APP_LOG_H

#include <stdarg.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===========================================================================
 *  Global logging switch.
 *  - Set PRINT_LOG_ENABLE to 0 to compile out ALL application logging.
 *    Every PRINT_LOG(...) call becomes a no-op, and printf_log() returns
 *    immediately, so there is zero runtime cost and no UART traffic.
 *  - Set to 1 (default) to enable logging.
 * ========================================================================= */
#ifndef PRINT_LOG_ENABLE
#define PRINT_LOG_ENABLE 1
#endif

/**
 * @brief  Thread-safe, crash-safe formatted log. Replaces printf() in all
 *         application tasks. It formats into a stack buffer and pushes the
 *         result through uart_write(), which already handles:
 *           - mutex protection when the RTOS scheduler is running
 *           - blocking polled transmit when the scheduler is NOT running
 *             (boot stage, pre-vTaskStartScheduler, or after a crash)
 *         so it is safe to call from tasks, hooks, and pre-scheduler code.
 *
 *         It never calls the newlib printf path (which would re-enter the
 *         UART transmit mutex and could deadlock). The stack buffer means
 *         it does not allocate from the FreeRTOS heap, so it works even if
 *         the heap is corrupted/not yet initialised.
 *
 * @note   Do NOT call from an ISR. Use uart_puts() (or a FromISR variant) in
 *         interrupt context instead.
 */
void printf_log(const char *fmt, ...);

/* Same, with an explicit va_list (for wrappers). */
void vprintf_log(const char *fmt, va_list ap);

/**
 * @brief  The recommended logging macro for application code.
 *         - When PRINT_LOG_ENABLE == 1 it expands to printf_log(...).
 *         - When PRINT_LOG_ENABLE == 0 it expands to nothing (compiled out).
 *         Supports a variable argument list and is safe to call whether or
 *         not the RTOS scheduler has started.
 */
#if PRINT_LOG_ENABLE
  #define PRINT_LOG(fmt, ...)   printf_log(fmt, ##__VA_ARGS__)
#else
  #define PRINT_LOG(fmt, ...)   ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* __APP_LOG_H */
