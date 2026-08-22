#ifndef __BSP_UART_H
#define __BSP_UART_H

#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "semphr.h"

/* Debug console on USART1: PA9(TX) / PA10(RX), 115200 8N1 */
extern UART_HandleTypeDef huart1;

/* Ring buffer sizes (power of two recommended) */
#define UART_RX_BUF_SIZE   256
#define UART_TX_BUF_SIZE   512

/**
  * @brief  Initialize USART1 (PA9/PA10) at 115200 8N1, enable RXNE interrupt
  *         and prepare the TX interrupt-driven transmitter. Retargets printf
  *         (via syscalls _write -> uart_puts).
  */
void BSP_UART_Init(void);

/**
  * @brief  Put a NUL-terminated string into the TX ring buffer and start the
  *         transmitter. Thread-safe (mutex protected). Returns number of bytes
  *         queued (may be less than len if the buffer is full).
  */
int  uart_puts(const char *s);
int  uart_write(const uint8_t *data, int len);

/**
  * @brief  Non-blocking get one received byte. Returns 1 and stores the byte
  *         if available, otherwise returns 0.
  */
int  uart_getc(uint8_t *c);

/* USART1 interrupt service routine (defined in bsp_uart.c) forwards to this. */
void BSP_UART_IRQHandler(void);

#endif /* __BSP_UART_H */
