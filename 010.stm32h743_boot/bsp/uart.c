/**
  ******************************************************************************
  * @file    bsp/uart.c
  * @brief   UART debug output driver (USART1, PA9/PA10 -> ST-Link VCP)
  ******************************************************************************
  */
#include "uart.h"
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

static UART_HandleTypeDef huart1;

/* RAM log mirror buffer (see uart.h) */
char             g_uart_log[UART_LOG_BUF_SIZE];
volatile uint32_t g_uart_log_len = 0;

HAL_StatusTypeDef BSP_UART_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* Enable clocks */
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA9 = USART1_TX (AF7), PA10 = USART1_RX (AF7) */
    gpio.Pin       = GPIO_PIN_9 | GPIO_PIN_10;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &gpio);

    huart1.Instance          = BSP_UART_INSTANCE;
    huart1.Init.BaudRate     = BSP_UART_BAUDRATE;
    huart1.Init.WordLength   = UART_WORDLENGTH_8B;
    huart1.Init.StopBits     = UART_STOPBITS_1;
    huart1.Init.Parity       = UART_PARITY_NONE;
    huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart1.Init.Mode         = UART_MODE_TX_RX;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;

    return HAL_UART_Init(&huart1);
}

void BSP_UART_SendStr(const char *str)
{
    if (str == NULL) return;
    uint16_t len = (uint16_t)strlen(str);
    HAL_UART_Transmit(&huart1, (uint8_t *)str, len, HAL_MAX_DELAY);
}

void BSP_UART_SendBuf(const uint8_t *buf, uint16_t len)
{
    if (buf == NULL || len == 0) return;
    HAL_UART_Transmit(&huart1, (uint8_t *)buf, len, HAL_MAX_DELAY);
}

int BSP_UART_Printf(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (n > 0) {
        uint16_t len = (uint16_t)((n < (int)sizeof(buf)) ? n : (int)sizeof(buf) - 1);
        HAL_UART_Transmit(&huart1, (uint8_t *)buf, len, HAL_MAX_DELAY);

        /* mirror to RAM log for SWD retrieval */
        for (uint16_t i = 0; i < len && g_uart_log_len < UART_LOG_BUF_SIZE - 1; i++) {
            g_uart_log[g_uart_log_len++] = buf[i];
        }
        g_uart_log[g_uart_log_len] = '\0';
    }
    return n;
}
