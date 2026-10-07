/**
 * @file bsp_uart.h
 * @brief Debug console UART (SCI9 @ P208/P209, 115200 8N1)
 */
#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdbool.h>
#include <stdint.h>

#define BSP_UART_BAUDRATE    (115200U)

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Open SCI9 and register the RX interrupt. Must run before any kprintf. */
void bsp_uart_init(void);

/** @brief Blocking polled single-char TX. Safe from ISR / critical sections. */
void bsp_uart_putc(char c);

/** @brief Write a buffer, expanding '\n' to "\r\n". */
void bsp_uart_write(const char * s, uint32_t len);

/** @brief Block until one character is received (interrupt-driven ring buffer). */
char bsp_uart_getchar(void);

/** @brief Number of characters currently pending in the RX ring buffer. */
uint32_t bsp_uart_rx_pending(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_UART_H */
