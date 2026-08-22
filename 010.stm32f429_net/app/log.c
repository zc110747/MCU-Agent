/**
  ******************************************************************************
  * @file    log.c
  * @brief   Crash-safe formatted logging for STM32F429 (FreeRTOS + LwIP).
  *
  *   printf_log() is a drop-in replacement for printf() that:
  *     1. Never re-enters the UART transmit mutex (no newlib printf path).
  *     2. Uses a stack-local format buffer -> no FreeRTOS heap allocation,
  *        so it still works before the scheduler starts or if the heap is
  *        corrupted.
  *     3. Routes everything through uart_write(), which itself distinguishes
  *        the two runtime states:
  *            - RTOS scheduler RUNNING      -> mutex-protected ring enqueue
  *            - RTOS scheduler NOT running  -> blocking polled HAL transmit
  *        => printf_log() is safe both before vTaskStartScheduler() (boot
  *        log) and inside running tasks.
  *
  *   PRINT_LOG(fmt, ...) is the preferred macro: when PRINT_LOG_ENABLE is 0
  *   it compiles to nothing; printf_log() also early-returns, so logging can
  *   be switched off globally with zero cost.
  ******************************************************************************
  */
#include "log.h"
#include "bsp_uart.h"
#include <stdio.h>
#include <string.h>

/* Per-call format buffer. 160 bytes covers all current log lines; longer
 * lines are truncated by vsnprintf (it returns the would-be length). */
#define LOG_BUF_SIZE 160

void vprintf_log(const char *fmt, va_list ap)
{
#if PRINT_LOG_ENABLE == 0
  (void)fmt; (void)ap;
  return;
#else
  char buf[LOG_BUF_SIZE];
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  if (n < 0) return;
  if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
  uart_write((const uint8_t *)buf, n);
#endif
}

void printf_log(const char *fmt, ...)
{
#if PRINT_LOG_ENABLE == 0
  (void)fmt;
  return;
#else
  va_list ap;
  va_start(ap, fmt);
  vprintf_log(fmt, ap);
  va_end(ap);
#endif
}
