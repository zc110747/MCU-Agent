/**
  ******************************************************************************
  * @file    main.c
  * @brief   STM32F429IGT6 + LwIP (NO_SYS) network demo.
  *          - LAN8720 PHY (released from reset via PCF8574 P7)
  *          - Static IPv4 192.168.10.99
  *          - Minimal HTTP server (port 80) serving a flash page
  *          - LED heartbeat, USART1(PA9/PA10) debug console
  ******************************************************************************
  */
#include "stm32f4xx_hal.h"
#include <stdio.h>

#include "bsp_led.h"
#include "bsp_uart.h"
#include "bsp_i2c.h"
#include "bsp_pcf8574.h"
#include "lwip.h"
#include "http_server.h"

static void SystemClock_Config(void);
void Error_Handler(void);

int main(void)
{
  /* Reset of all peripherals, initializes the Flash interface and SysTick */
  HAL_Init();

  /* Configure the system clock to 180 MHz (HSE 25 MHz -> PLL) */
  SystemClock_Config();

  /* Debug console */
  BSP_UART_Init();
  printf("\r\nSTM32F429 NET demo starting...\r\n");

  /* Board status LED */
  BSP_LED_Init();
  BSP_LED_On(1);

  /* I2C2 + PCF8574 used to release the ETH PHY from reset */
  BSP_I2C_Init();
  BSP_ETH_PHY_Reset();
  printf("ETH PHY (LAN8720) released from reset.\r\n");

  /* LwIP stack (NO_SYS) */
  MX_LWIP_Init();
  http_server_init();
  printf("LwIP ready. Target IP = 192.168.10.99\r\n");

  uint32_t led_toggle_ms = 0U;

  /* Main loop */
  while (1)
  {
    /* LwIP: process RX, link state, timeouts */
    MX_LWIP_Process();

    /* Heartbeat LED */
    uint32_t now = HAL_GetTick();
    if ((now - led_toggle_ms) >= 500U)
    {
      led_toggle_ms = now;
      BSP_LED_Toggle(0);
    }
  }
}

/**
  * @brief  System Clock Configuration
  *         The system Clock is configured as follow:
  *            System Clock source            = PLL (HSE)
  *            SYSCLK(Hz)                     = 180000000
  *            HCLK(Hz)                       = 180000000
  *            AHB Prescaler                  = 1
  *            APB1 Prescaler                 = 4 (45 MHz)
  *            APB2 Prescaler                 = 2 (90 MHz)
  *            HSE Frequency(Hz)              = 25000000
  *            PLL_M                          = 25
  *            PLL_N                          = 360
  *            PLL_P                          = 2
  *            PLL_Q                          = 8 (45 MHz for I2S)
  */
static void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 360;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 8;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
    BSP_LED_Toggle(1);
    for (volatile uint32_t i = 0; i < 0x1FFFF; i++) { }
  }
}
