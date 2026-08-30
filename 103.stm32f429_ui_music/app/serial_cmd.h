/**
  ******************************************************************************
  * @file    app/serial_cmd.h
  * @brief   USART3 line-oriented command console (see serial_cmd.c).
  ******************************************************************************
  */
#ifndef SERIAL_CMD_H
#define SERIAL_CMD_H

#include <stdint.h>

/* Create the serial command task.  Safe to call once the scheduler is up; the
 * task polls USART3 and is fully decoupled from the UI render pump, so it keeps
 * working (and keeps the board observable) even if LVGL is busy. */
void serial_cmd_init(void);

/* ---- Debugger command injection -------------------------------------------
 * The board's debug header exposes USART3 TX only: PB11 (RX) is not driven by
 * the on-board USB-UART bridge, so host->target characters never arrive and
 * uart_getchar_nowait() can never return anything.  Verified on hardware -
 * with the host transmitting continuously, GPIOB_IDR bit 11 stays high in
 * 20/20 samples and USART3_SR.RXNE never sets, while CR1 has RE=1 and both
 * PB10/PB11 are correctly muxed to AF7.
 *
 * A debugger can therefore drive the console by writing a NUL-terminated
 * command string into g_dbg_line and then setting g_dbg_pending to 1.  The
 * console task picks it up on its next poll and dispatches it through the
 * SAME path the UART would use, so every command (including the stress loop)
 * works without a working RX line.  Inert unless a debugger writes it.
 *
 * OpenOCD:  mwb <addr> <byte>   ... then   mwb <pending_addr> 1
 */
extern volatile char   g_dbg_line[16];
extern volatile uint8_t g_dbg_pending;

#endif /* SERIAL_CMD_H */
