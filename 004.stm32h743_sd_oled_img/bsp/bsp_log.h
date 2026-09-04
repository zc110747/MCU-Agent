/**
  ******************************************************************************
  * @file    bsp_log.h
  * @brief   Crash-safe USART1 console: non-blocking PRINT_LOG (TX ring buffer
  *         + TXE interrupt drain).
  ******************************************************************************
  */

#ifndef __BSP_LOG_H
#define __BSP_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stdint.h>
#include "main.h"

/* ===========================================================================
 *  Global logging switch.
 *  - Set PRINT_LOG_ENABLE to 0 to compile out ALL application logging.
 *    Every PRINT_LOG(...) call becomes a no-op, so there is zero runtime cost
 *    and no UART traffic.
 *  - Set to 1 (default) to enable logging.
 * ========================================================================= */
#ifndef PRINT_LOG_ENABLE
#define PRINT_LOG_ENABLE 1
#endif

/** Initialise USART1 (115200-8-N-1) and arm the TX interrupt. */
GlobalType_t bsp_log_init(void);

/** Blocking raw write (kept for compatibility / newlib _write retarget). */
void bsp_log_write(const char *data, int len);

/**
 * @brief  Crash-safe formatted log for the bare-metal STM32H743 build.
 *         Formats into a stack buffer and pushes the result into a TX ring
 *         buffer; a USART1 transmit (TXE) interrupt drains the ring in the
 *         background, so the call never blocks on the UART.
 *
 * @note   Do NOT call from an ISR. Use log_uart_tx_irq() (ISR side) instead.
 */
void printf_log(const char *fmt, ...);

/* Same, with an explicit va_list (for wrappers). */
void vprintf_log(const char *fmt, va_list ap);

/**
 * @brief  IRQ-side drain hook.  Call this from USART1_IRQHandler() so the
 *         pending bytes in the TX ring buffer are shifted out one by one.
 */
void log_uart_tx_irq(void);

/**
 * @brief  Enable the USART1 global interrupt (NVIC).  Called once by
 *         bsp_log_init(); idempotent, and lazily re-armed by uart_write().
 */
void log_uart_init(void);

/* ---------------------------------------------------------------------------
 *  Recommended logging macro for application code.
 *  - When PRINT_LOG_ENABLE == 1 it expands to printf_log(...).
 *  - When PRINT_LOG_ENABLE == 0 it expands to nothing (compiled out):
 *    no UART traffic and zero runtime cost.
 *
 *  Set -DPRINT_LOG_ENABLE=0 on the command line / in CMake to disable all
 *  output globally.
 * ------------------------------------------------------------------------- */
#if PRINT_LOG_ENABLE
  #define PRINT_LOG(fmt, ...)    printf_log(fmt, ##__VA_ARGS__)
#else
  #define PRINT_LOG(fmt, ...)    ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* __BSP_LOG_H */
