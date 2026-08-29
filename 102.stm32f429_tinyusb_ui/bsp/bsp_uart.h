#ifndef __BSP_UART_H
#define __BSP_UART_H

#include "stm32f4xx_hal.h"

/* Debug console on USART3: PB10(TX) / PB11(RX), 115200 8N1.
 * IMPORTANT: BSP_UART_Init() creates NO FreeRTOS object.  This project brings
 * the console up BEFORE the SDRAM and therefore before vPortDefineHeapRegions()
 * (see main.c), so anything that allocates from ucHeap here would corrupt the
 * heap free list.  The TX mutex is created lazily on first use instead.
 *
 * printf() is retargeted through syscalls _write -> uart_write(), but
 * application code must log through PRINT_LOG() (app/log.h) instead - that is
 * what the global PRINT_LOG_ENABLE switch controls. */
extern UART_HandleTypeDef huart3;

/* 2048, not 512.  uart_write() DROPS bytes when the ring is full, and at
 * 115200 baud (11.5 kB/s) a 512 byte ring overflows as soon as one task
 * prints a burst -- which silently swallowed log lines printed by other
 * tasks (the SD/USB loader messages, for example).  Must stay a power of
 * two: the head/tail wrap relies on it. */
#define UART_TX_BUF_SIZE   2048
#define UART_RX_BUF_SIZE   64   /* small, polled-drain; not used by the demo */

/**
  * @brief  Initialize USART3 (PB10/PB11) at 115200 8N1 and the TX ring.
  *         No FreeRTOS object is created here (see the note above).
  */
void BSP_UART_Init(void);

/**
  * @brief  Put data into the TX ring and start the transmitter.
  *
  *         Two transmit paths, chosen at call time:
  *           - RTOS scheduler RUNNING      -> mutex-protected ring enqueue,
  *                                            drained by the TXE interrupt
  *           - RTOS scheduler NOT running  -> blocking polled HAL transmit
  *             (or the UART not initialised yet)
  *
  * @return bytes accepted.  NOTE this can be less than len: on the interrupt
  *         path bytes are dropped when the ring fills up.
  */
int  uart_write(const uint8_t *data, int len);
int  uart_puts(const char *s);

/**
  * @brief  Block until the TX ring is fully drained by the USART3 ISR
  *         (transmitter idle).  Use before a reset/reboot so the last message
  *         really reaches the wire.  Has a bounded wait so a stuck transmitter
  *         can never hang the caller.
  */
void uart_flush(void);

/* USART3 interrupt service routine (defined in bsp_uart.c). */
void BSP_UART_IRQHandler(void);

#endif /* __BSP_UART_H */
