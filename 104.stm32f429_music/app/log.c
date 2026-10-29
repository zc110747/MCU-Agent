/**
  ******************************************************************************
  * @file    log.c
  * @brief   Crash-safe formatted logging for STM32F429 (FreeRTOS + TinyUSB).
  *
  *   printf_log() is the single logging sink behind the PRINT_LOG() macro:
  *     1. It never re-enters the UART transmit lock: the text is formatted by
  *        vsnprintf() into a stack buffer and handed to uart_write() with an
  *        explicit length, so the newlib printf/_write path is not used.
  *     2. The buffer is stack-local -> no FreeRTOS heap allocation, so logging
  *        still works before the scheduler starts or if the heap is corrupt.
  *     3. uart_write() distinguishes the two runtime states:
  *            - RTOS scheduler RUNNING      -> mutex-protected ring enqueue
  *            - RTOS scheduler NOT running  -> blocking polled HAL transmit
  *        so printf_log() is safe both in the boot phase and in tasks.
  *
  *   When PRINT_LOG_ENABLE is 0 the macro compiles to nothing and both
  *   functions early-return, so logging can be switched off globally with
  *   zero cost.
  *
  *   NOTE ON LONG LINES
  *   -----------------
  *   Lines longer than LOG_BUF_SIZE-1 are truncated by vsnprintf() rather than
  *   split.  Keep log lines short; the disk-dump path does not use PRINT_LOG
  *   at all (it streams through uart_write() directly, see usb_host_app.c).
  ******************************************************************************
  */
#include "log.h"
#include "bsp_uart.h"
#include <stdio.h>

/* Per-call format buffer, on the stack.  192 bytes covers the longest line
 * the project prints today (the [TOUCH] ready line is ~110 chars).  The
 * smallest task stack here is 512 words (2 KB), so this is comfortable.
 * vsnprintf() returns the would-be length, so overlong lines are truncated
 * instead of overflowing. */
#define LOG_BUF_SIZE 192

void vprintf_log(const char *fmt, va_list ap)
{
#if PRINT_LOG_ENABLE == 0
  (void)fmt; (void)ap;
  return;
#else
  char buf[LOG_BUF_SIZE];
  int  n = vsnprintf(buf, sizeof(buf), fmt, ap);

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
