/**
 * @file    log.c
 * @brief   Crash-safe formatted logging for STM32F429 (FreeRTOS + SD/FatFs).
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
    (void)fmt;
    (void)ap;
    return;
#else
    char buf[LOG_BUF_SIZE];
    int  n = vsnprintf(buf, sizeof(buf), fmt, ap);

    if (n < 0)
        return;
    if (n > (int)sizeof(buf) - 1)
        n = (int)sizeof(buf) - 1;

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
