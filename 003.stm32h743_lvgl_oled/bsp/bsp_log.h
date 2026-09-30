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
 * @brief  Crash-safe formatted log for the bare-metal STM32H743 build.
 *         Formats into a stack buffer and pushes the result into a TX ring
 *         buffer; a USART1 transmit (TXE) interrupt drains the ring in the
 *         background, so the call never blocks on the UART.  No RTOS
 *         primitives are used.
 *
 * @note   Do NOT call from an ISR.  Use log_uart_tx_irq() (ISR side) instead.
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
 * @brief  Enable the USART1 global interrupt (NVIC).  Must be called once
 *         after MX_USART1_UART_Init(); uart_write() also enables it lazily as
 *         a safety net.
 */
void log_uart_init(void);

/**
 * @brief  The recommended logging macro for application code.
 *         - When PRINT_LOG_ENABLE == 1 it expands to printf_log(...).
 *         - When PRINT_LOG_ENABLE == 0 it expands to nothing (compiled out).
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
