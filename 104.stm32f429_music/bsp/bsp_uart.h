#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Debug console on USART1: PA9(TX) / PA10(RX), 115200 8N1.
 * bsp_uart_init() creates NO FreeRTOS object: it runs before the SDRAM and
 * therefore before vPortDefineHeapRegions() (see main.c), so anything that
 * allocates from ucHeap there would corrupt the heap free list.  The TX mutex
 * is created lazily on first use instead.
 *
 * printf() is retargeted through syscalls _write -> uart_write(), but
 * application code must log through PRINT_LOG() (app/log.h) instead - that is
 * what the global PRINT_LOG_ENABLE switch controls. */
extern UART_HandleTypeDef huart1;

/* 2048, not 512: uart_write() DROPS bytes when the ring is full, and at
 * 115200 baud (11.5 kB/s) a 512 byte ring overflows as soon as one task
 * prints a burst, which silently swallowed other tasks' log lines.  Must stay
 * a power of two: the head/tail wrap relies on it. */
#define UART_TX_BUF_SIZE 2048U
#define UART_RX_BUF_SIZE 64U /* small, polled-drain; not used by the demo */

/**
 * @brief  Initialize USART1 (PA9/PA10) at 115200 8N1 and the TX ring.
 */
void bsp_uart_init(void);

/**
 * @brief  Put data into the TX ring and start the transmitter.
 *
 *         Two transmit paths, chosen at call time:
 *           - scheduler RUNNING      -> mutex-protected ring enqueue,
 *                                       drained by the TXE interrupt
 *           - scheduler NOT running  -> blocking polled HAL transmit
 *
 * @return bytes accepted; can be less than len when the ring fills up.
 */
int32_t uart_write(const uint8_t *data, int32_t len);
int32_t uart_puts(const char *s);

/**
 * @brief  Block until the TX ring is fully drained (transmitter idle).
 *         Bounded wait, so a stuck transmitter can never hang the caller.
 */
void uart_flush(void);

/* USART1 interrupt service routine (defined in bsp_uart.c). */
void bsp_uart_irq_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_UART_H */
