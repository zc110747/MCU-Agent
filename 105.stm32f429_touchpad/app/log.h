/**
  ******************************************************************************
  * @file    log.h
  * @brief   Application-wide logging with a single compile-time on/off switch.
  *
  *  PRINT_LOG(fmt, ...) is the ONLY logging primitive application code should
  *  use.  It replaces printf() everywhere so that:
  *
  *   1. the whole log can be compiled out with one macro (PRINT_LOG_ENABLE=0),
  *      leaving zero code, zero RAM and zero UART traffic;
  *   2. logging never goes through the newlib printf path - it formats into a
  *      stack buffer and pushes it at uart_write() with an explicit length,
  *      so it cannot re-enter the UART transmit lock;
  *   3. it is safe before the FreeRTOS scheduler starts (boot log), inside any
  *      task, and even when the heap is corrupt - it never allocates.
  *
  *  The switch is normally supplied by the build system (CMake option
  *  ENABLE_PRINT_LOG -> -DPRINT_LOG_ENABLE=0|1).  The #ifndef below only
  *  provides a default for builds that do not define it.
  ******************************************************************************
  */
#ifndef __APP_LOG_H
#define __APP_LOG_H

#include <stdarg.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===========================================================================
 *  Global logging switch.
 *  - PRINT_LOG_ENABLE = 0 : every PRINT_LOG(...) becomes a no-op and
 *    printf_log()/vprintf_log() return immediately, so there is no runtime
 *    cost and no UART traffic at all.
 *  - PRINT_LOG_ENABLE = 1 : logging enabled (default).
 * ========================================================================= */
#ifndef PRINT_LOG_ENABLE
#define PRINT_LOG_ENABLE 1
#endif

/**
  * @brief  Formatted log, printf() compatible (minus float support).
  *
  *         Formats into a stack-local buffer (no heap) and pushes the result
  *         through uart_write(), which itself picks the right transmit path:
  *           - RTOS scheduler RUNNING     -> mutex-protected ring enqueue
  *           - RTOS scheduler NOT running -> blocking polled transmit
  *         so it is safe before vTaskStartScheduler() (boot log) and from any
  *         running task.
  *
  * @note   Do NOT call from an ISR - use uart_write()/uart_puts() there.
  */
void printf_log(const char *fmt, ...);

/* Same, with an explicit va_list (for wrappers). */
void vprintf_log(const char *fmt, va_list ap);

#if PRINT_LOG_ENABLE
  #define PRINT_LOG(fmt, ...)   printf_log(fmt, ##__VA_ARGS__)
#else
  #define PRINT_LOG(fmt, ...)   ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* __APP_LOG_H */
