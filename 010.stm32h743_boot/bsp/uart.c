/**
  ******************************************************************************
  * @file    bsp/uart.c
  * @brief   UART compatibility layer (USART1, PA9/PA10 -> ST-Link VCP).
  *
  *   The UART itself is owned by bsp_log.c (non-blocking, TXE-interrupt driven).
  *   This file keeps:
  *     - the RAM log mirror (g_uart_log) used by the SWD dump path
  *     - BSP_UART_Init() as a compatibility wrapper around bsp_log_init()
  *     - BSP_UART_SendStr()/SendBuf() as thin raw-output helpers so pre-existing
  *       callers keep working without pulling in another HAL UART handle.
  ******************************************************************************
  */
#include "uart.h"
#include "bsp_log.h"
#include <string.h>

/* RAM log mirror (see uart.h) */
char             g_uart_log[UART_LOG_BUF_SIZE];
volatile uint32_t g_uart_log_len = 0;

void BSP_UART_LogMirror(const char *data, uint32_t len)
{
    if (data == NULL) return;

    for (uint32_t i = 0; i < len && g_uart_log_len < UART_LOG_BUF_SIZE - 1U; i++) {
        g_uart_log[g_uart_log_len++] = data[i];
    }
    g_uart_log[g_uart_log_len] = '\0';
}

HAL_StatusTypeDef BSP_UART_Init(void)
{
    /* USART1 (PA9/PA10, 115200 8N1) is fully configured by the logger; the
       return value is kept for callers that treat a UART failure as fatal.
       HAL + clocks must already be initialized by the caller. */
    bsp_log_init();
    return HAL_OK;
}

void BSP_UART_SendStr(const char *str)
{
    if (str == NULL) return;
    /* Routed through the logger so the line lands in the RAM mirror too. */
    printf_log("%s", str);
}

void BSP_UART_SendBuf(const uint8_t *buf, uint16_t len)
{
    if (buf == NULL || len == 0) return;

    for (uint16_t i = 0; i < len; i++) {
        printf_log("%c", (char)buf[i]);
    }
}
