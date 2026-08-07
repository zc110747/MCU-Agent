/**
 * @file    drv_uart.c
 * @brief   USART1 debug console on PA9/PA10.
 */
#include "drv_uart.h"

UART_HandleTypeDef huart1;

static uint8_t s_ready;

GlobalType_t drv_uart_init(void)
{
    huart1.Instance                    = USART1;
    huart1.Init.BaudRate               = DBG_UART_BAUDRATE;
    huart1.Init.WordLength             = UART_WORDLENGTH_8B;
    huart1.Init.StopBits               = UART_STOPBITS_1;
    huart1.Init.Parity                 = UART_PARITY_NONE;
    huart1.Init.Mode                   = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling           = UART_OVERSAMPLING_16;
    huart1.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
    huart1.Init.ClockPrescaler         = UART_PRESCALER_DIV1;
    huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&huart1) != HAL_OK)
    {
        return RT_FAIL;
    }

    s_ready = 1u;
    return RT_OK;
}

void drv_uart_write(const uint8_t *data, uint16_t len)
{
    if (!s_ready || (data == NULL) || (len == 0u))
    {
        return;
    }
    /* Blocking on purpose: log output must never reorder against the code
     * that produced it while single stepping in the debugger. */
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)data, len, 100u);
}
