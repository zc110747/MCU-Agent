/**
  ******************************************************************************
  * @file    bsp/uart.h
  * @brief   UART compatibility layer (USART1, PA9/PA10 -> ST-Link VCP).
  *
  *   All formatting/sending is done by the bsp_log module (non-blocking TX ring
  *   buffer drained by the USART1 TXE interrupt). This header only keeps the
  *   legacy entry points that the boot path and the tools rely on:
  *
  *     BSP_UART_Init()   - bring up USART1 (delegates to bsp_log_init())
  *     BSP_UART_SendStr()/SendBuf() - raw string/buffer output, formatted
  *                         through printf_log() so the RAM mirror stays complete
  *     g_uart_log        - RAM mirror of everything the logger emitted, so the
  *                         full boot log can still be dumped over SWD
  *                         (openocd dump_image) when the VCP capture is flaky.
  *
  *   NOTE: application code should use PRINT_LOG() from bsp_log.h instead of
  *   calling BSP_UART_* directly.
  ******************************************************************************
  */
#ifndef __BSP_UART_H
#define __BSP_UART_H

#include "stm32h7xx_hal.h"

#define BSP_UART_INSTANCE   USART1
#define BSP_UART_BAUDRATE   115200

/* RAM log mirror: every byte emitted by the logger is also appended here,
   so the full test output can be retrieved over SWD (openocd dump_image)
   even when the ST-Link VCP / UART capture is unavailable. */
#define UART_LOG_BUF_SIZE   16384U
extern char             g_uart_log[UART_LOG_BUF_SIZE];
extern volatile uint32_t g_uart_log_len;

/**
  * @brief  Append raw bytes to the RAM log mirror (no UART access).
  *         Called by printf_log() in bsp_log.c for every emitted byte.
  */
void BSP_UART_LogMirror(const char *data, uint32_t len);

HAL_StatusTypeDef BSP_UART_Init(void);
void BSP_UART_SendStr(const char *str);
void BSP_UART_SendBuf(const uint8_t *buf, uint16_t len);

#endif /* __BSP_UART_H */
