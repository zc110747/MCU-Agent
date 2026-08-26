#ifndef __BSP_UART_H
#define __BSP_UART_H

#include "stm32f4xx_hal.h"

/* Debug console on USART3: PB10(TX) / PB11(RX), 115200 8N1.
 * IMPORTANT: this driver intentionally uses NO FreeRTOS objects (no queue,
 * no mutex).  The TX path is a simple critical-section ring buffer drained by
 * the USART3 TXE interrupt, so BSP_UART_Init() is safe to call BEFORE the
 * SDRAM (and therefore before the FreeRTOS heap) is up -- it never allocates
 * from ucHeap.  Retargets printf via syscalls _write -> uart_write(). */
extern UART_HandleTypeDef huart3;

#define UART_TX_BUF_SIZE   512
#define UART_RX_BUF_SIZE   64   /* small, polled-drain; not used by the demo */

/**
  * @brief  Initialize USART3 (PB10/PB11) at 115200 8N1 and the TX ring.
  *         No FreeRTOS object is created here.
  */
void BSP_UART_Init(void);

/**
  * @brief  Put data into the TX ring and start the transmitter.  Falls back
  *         to a blocking polled transmit before the UART IRQ is live or when
  *         the scheduler is not running.  Returns bytes queued/polled.
  */
int  uart_write(const uint8_t *data, int len);
int  uart_puts(const char *s);

/* USART3 interrupt service routine (defined in bsp_uart.c). */
void BSP_UART_IRQHandler(void);

#endif /* __BSP_UART_H */
