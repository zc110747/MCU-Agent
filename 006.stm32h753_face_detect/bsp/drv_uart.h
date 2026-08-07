/**
 * @file    drv_uart.h
 * @brief   USART1 debug console on PA9 (TX) / PA10 (RX), 115200 8N1.
 *
 * printf() is retargeted here through _write() in Core/Src/syscalls.c, so the
 * PRINT_LOG() macros in logger.h come out of the SWD/USART1 header.
 */
#ifndef __DRV_UART_H
#define __DRV_UART_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define DBG_UART_BAUDRATE       115200u

GlobalType_t drv_uart_init(void);

/** Blocking send, used by _write(). Safe to call before init (drops data). */
void drv_uart_write(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_UART_H */
