/**
  ******************************************************************************
  * @file    bsp_console.h
  * @brief   Unified byte stream over the debug UART and the USB CDC port.
  *
  *  Both links carry the same protocol and are treated as one console: bytes
  *  arriving on either are merged into a single RX ring, and everything the
  *  firmware prints goes out on both.  That way the board can be driven from
  *  the ST-Link VCP (USART1, PA9/PA10) or from the USB-C port (PA11/PA12,
  *  tinyusb CDC) without changing a line of application code.
  *
  *  printf() lands here too: this file defines a strong _write(), overriding
  *  the weak UART-only one in Core/Src/syscalls.c.
  ******************************************************************************
  */
#ifndef __BSP_CONSOLE_H
#define __BSP_CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** Where a received byte came from - reported by bsp_console_last_source(). */
typedef enum
{
    CONSOLE_SRC_NONE = 0,
    CONSOLE_SRC_UART,
    CONSOLE_SRC_USB
} console_src_t;

void          bsp_console_init(void);

/** Pump the USB stack and drain its RX FIFO.  Call from the main loop. */
void          bsp_console_task(void);

/** Next received byte, or -1 when nothing is queued. */
int           bsp_console_getc(void);

console_src_t bsp_console_last_source(void);

void          bsp_console_write(const char *data, uint32_t len);
void          bsp_console_puts(const char *text);

/** 1 when a host has opened the USB CDC port (DTR asserted). */
int           bsp_console_usb_ready(void);

/** Push a byte into the RX ring - used by the USART1 interrupt handler. */
void          bsp_console_rx_push(uint8_t byte, console_src_t src);

/** Interrupt service routine body for USART1, called from stm32h7xx_it.c. */
void          bsp_console_uart_irq(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_CONSOLE_H */
