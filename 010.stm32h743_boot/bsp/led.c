/**
  ******************************************************************************
  * @file    bsp/  led.c
  * @brief   Status LED driver (PG7, active-low, push-pull, pull-up)
  ******************************************************************************
  */
#include "led.h"

void BSP_LED_Init(void)
{
    __HAL_RCC_GPIOG_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin       = LED_GPIO_PIN;
    gpio.Mode      = GPIO_MODE_OUTPUT_PP;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Speed     = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_GPIO_PORT, &gpio);

    BSP_LED_Off();   /* start OFF (idle-high), first blink after LED_BLINK_MS */
}

void BSP_LED_On(void)
{
    HAL_GPIO_WritePin(LED_GPIO_PORT, LED_GPIO_PIN, GPIO_PIN_RESET);  /* LOW = on */
}

void BSP_LED_Off(void)
{
    HAL_GPIO_WritePin(LED_GPIO_PORT, LED_GPIO_PIN, GPIO_PIN_SET);    /* HIGH = off */
}

void BSP_LED_Toggle(void)
{
    HAL_GPIO_TogglePin(LED_GPIO_PORT, LED_GPIO_PIN);
}
