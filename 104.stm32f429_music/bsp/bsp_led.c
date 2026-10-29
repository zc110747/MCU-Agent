/**
 ******************************************************************************
 * @file    bsp_led.c
 * @brief   LED0 (PB1) / LED1 (PB0) indicators, low active.
 ******************************************************************************
 */
#include "bsp_led.h"

/**
 * @brief  Initialize both LEDs as push-pull outputs and drive them off.
 */
void bsp_led_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin   = LED0_PIN | LED1_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* Off = high level */
    HAL_GPIO_WritePin(LED0_PORT, LED0_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LED1_PORT, LED1_PIN, GPIO_PIN_SET);
}

void bsp_led_on(uint8_t led)
{
    if (led == 0U)
    {
        HAL_GPIO_WritePin(LED0_PORT, LED0_PIN, GPIO_PIN_RESET);
    }
    else
    {
        HAL_GPIO_WritePin(LED1_PORT, LED1_PIN, GPIO_PIN_RESET);
    }
}

void bsp_led_off(uint8_t led)
{
    if (led == 0U)
    {
        HAL_GPIO_WritePin(LED0_PORT, LED0_PIN, GPIO_PIN_SET);
    }
    else
    {
        HAL_GPIO_WritePin(LED1_PORT, LED1_PIN, GPIO_PIN_SET);
    }
}

void bsp_led_toggle(uint8_t led)
{
    if (led == 0U)
    {
        HAL_GPIO_TogglePin(LED0_PORT, LED0_PIN);
    }
    else
    {
        HAL_GPIO_TogglePin(LED1_PORT, LED1_PIN);
    }
}
