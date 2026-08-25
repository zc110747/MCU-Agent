/**
  ******************************************************************************
  * @file    bsp/uart.h
  * @brief   UART debug output driver (USART1, PA9/PA10 -> ST-Link VCP)
  ******************************************************************************
  */
#ifndef __BSP_UART_H
#define __BSP_UART_H

#include "stm32h7xx_hal.h"

#define BSP_UART_INSTANCE   USART1
#define BSP_UART_BAUDRATE   115200

/* RAM log mirror: every byte sent via BSP_UART_Printf is also appended here,
   so the full test output can be retrieved over SWD (openocd dump_image)
   even when the ST-Link VCP / UART capture is unavailable. */
#define UART_LOG_BUF_SIZE   16384U
extern char             g_uart_log[UART_LOG_BUF_SIZE];
extern volatile uint32_t g_uart_log_len;

HAL_StatusTypeDef BSP_UART_Init(void);
int BSP_UART_Printf(const char *fmt, ...);
void BSP_UART_SendStr(const char *str);
void BSP_UART_SendBuf(const uint8_t *buf, uint16_t len);

#endif /* __BSP_UART_H */
