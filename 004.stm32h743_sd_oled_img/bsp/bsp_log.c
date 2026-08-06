/**
  ******************************************************************************
  * @file    bsp_log.c
  * @brief   USART1 console (PA9/PA10, 115200-8-N-1) + printf retarget.
  ******************************************************************************
  */

#include "bsp_log.h"

#include <unistd.h>

#define LOG_UART_TIMEOUT_MS   100U

GlobalType_t bsp_log_init(void)
{
    huart1.Instance                    = USART1;
    huart1.Init.BaudRate               = 115200;
    huart1.Init.WordLength             = UART_WORDLENGTH_8B;
    huart1.Init.StopBits               = UART_STOPBITS_1;
    huart1.Init.Parity                 = UART_PARITY_NONE;
    huart1.Init.Mode                   = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling           = UART_OVERSAMPLING_16;
    huart1.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
    huart1.Init.ClockPrescaler         = UART_PRESCALER_DIV1;
    huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&huart1) != HAL_OK) {
        return RT_FAIL;
    }
    if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK) {
        return RT_FAIL;
    }
    if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK) {
        return RT_FAIL;
    }
    if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK) {
        return RT_FAIL;
    }

    /* printf() must not wait for a full line buffer */
    setvbuf(stdout, NULL, _IONBF, 0);

    return RT_OK;
}

void bsp_log_write(const char *data, int len)
{
    if (data == NULL || len <= 0) {
        return;
    }
    HAL_UART_Transmit(&huart1, (uint8_t *)data, (uint16_t)len, LOG_UART_TIMEOUT_MS);
}

/* ---- newlib retarget ------------------------------------------------------ */

int _write(int file, char *ptr, int len)
{
    (void)file;
    bsp_log_write(ptr, len);
    return len;
}
