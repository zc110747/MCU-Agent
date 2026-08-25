/**
  ******************************************************************************
  * @file    test_app/app/main.c
  * @brief   Minimal app that proves the bootloader jumped here.
  *
  *   - sets its own vector table at 0x08020000
  *   - 1 Hz LED heartbeat (LED_BLINK_MS = 1000)
  *   - UART banner + heartbeat prints
  ******************************************************************************
  */
#include "stm32h7xx_hal.h"
#include "uart.h"
#include "led.h"

static void SystemClock_Config(void);
static void Error_Handler(void);

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    /* The bootloader already pointed VTOR here, but set it explicitly so the
       app is also runnable on its own (e.g. flashed directly). */
    SCB->VTOR = 0x08020000UL;

    if (BSP_UART_Init() != HAL_OK) {
        Error_Handler();
    }

    BSP_UART_Printf("\r\n");
    BSP_UART_Printf("========================================\r\n");
    /* Print the live version slot @0x08021000 so a successful upgrade is
       unmistakable (the slot is part of the flashed image). */
    {
        const uint8_t *app_ver = (const uint8_t *)0x08021000UL;
        BSP_UART_Printf("  STM32H743 TEST APP  v%u.%u.%u.%u\r\n",
                        app_ver[0], app_ver[1], app_ver[2], app_ver[3]);
    }
    BSP_UART_Printf("  (bootloader jumped here @0x08020000)\r\n");
    BSP_UART_Printf("========================================\r\n");

    BSP_LED_Init();

    while (1) {
        BSP_LED_Toggle();
        HAL_Delay(LED_BLINK_MS);          /* 1000 ms => 1 Hz heartbeat */
        BSP_UART_Printf(" app alive @1Hz\r\n");
    }
}

/* HSE 25MHz -> PLL1 -> 480 MHz sys / 240 MHz HCLK / 120 MHz PCLK (same as BL) */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    MODIFY_REG(PWR->CR3, PWR_CR3_SCUEN, 0);
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    while ((PWR->D3CR & PWR_D3CR_VOSRDY) != PWR_D3CR_VOSRDY) {}

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 5;
    RCC_OscInitStruct.PLL.PLLN = 192;
    RCC_OscInitStruct.PLL.PLLP = 2;
    RCC_OscInitStruct.PLL.PLLQ = 4;
    RCC_OscInitStruct.PLL.PLLR = 2;
    RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
    RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    RCC_OscInitStruct.PLL.PLLFRACN = 0;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK  | RCC_CLOCKTYPE_SYSCLK |
                                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
                                  RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
    RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_6) != HAL_OK) {
        Error_Handler();
    }

    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_QSPI;
    PeriphClkInitStruct.QspiClockSelection = RCC_QSPICLKSOURCE_D1HCLK;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK) {
        Error_Handler();
    }

    HAL_SYSTICK_Config(SystemCoreClock / 1000U);
    HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);
    HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);
}

static void Error_Handler(void)
{
    while (1) {
    }
}
