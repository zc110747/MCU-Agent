/**
  ******************************************************************************
  * @file    app/main.c
  * @brief   STM32H743ZIT6 QSPI flash exposed as a USB Mass Storage (U-disk)
  *
  * The firmware initialises the QSPI flash (HAL indirect mode only), prepares a
  * FAT volume for plug-and-play usability, then runs the TinyUSB MSC stack so a
  * host sees the 8 MB QSPI as a removable disk. No memory-mapped (XIP) access is
  * performed at boot, which keeps the QUADSPI peripheral in a clean indirect
  * state for both FatFs and the USB Mass Storage callbacks.
  ******************************************************************************
  */
#include "stm32h7xx_hal.h"
#include "uart.h"
#include "qspi.h"
#include "fs_init.h"
#include "usb_board.h"
#include "tusb.h"
#include "led.h"

static void SystemClock_Config(void);
static void Error_Handler(void);

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    /* Caches left disabled at reset; single-line QSPI indirect reads hit the
       device fresh - no stale cache lines across HAL / XIP mode switches. */

    if (BSP_UART_Init() != HAL_OK) {
        Error_Handler();
    }

    BSP_UART_Printf("\r\n");
    BSP_UART_Printf("========================================================\r\n");
    BSP_UART_Printf(" STM32H743ZIT6 QSPI U-Disk (USB MSC)\r\n");
    BSP_UART_Printf("========================================================\r\n");
    BSP_UART_Printf(" CPU Clock : %lu MHz\r\n", (unsigned long)(SystemCoreClock / 1000000UL));

    /* Initialise the QSPI peripheral (HAL indirect mode). No memory-mapped /
       XIP session is started, so the peripheral stays clean for FatFs and the
       USB MSC callbacks. */
    if (BSP_QSPI_Init() != QSPI_OK) {
        BSP_UART_Printf(" QSPI init FAILED\r\n");
        Error_Handler();
    }
    BSP_UART_Printf(" QSPI initialised (HAL indirect mode)\r\n");

    /* Status LED: 500 ms heartbeat blink = firmware running. */
    BSP_LED_Init();
    BSP_UART_Printf(" LED heartbeat started (PG7, 500 ms)\r\n");

    /* Prepare the FAT volume before USB is enabled: detect an existing FS, or
       format a blank flash, then unmount - so the host sees a clean disk. */
    FS_PrepareForMassStorage();

    /* Bring up the USB2_OTG_FS device and start the TinyUSB stack. */
    BSP_USB_Init();
    if (!tusb_init()) {
        BSP_UART_Printf(" tusb_init FAILED\r\n");
        Error_Handler();
    }
    BSP_UART_Printf(" USB MSC ready - connect the board to a host\r\n");

    uint32_t led_tick = 0;
    while (1) {
        /* Non-blocking blink: toggle every LED_BLINK_MS without stalling tud_task() */
        if ((HAL_GetTick() - led_tick) >= LED_BLINK_MS) {
            led_tick = HAL_GetTick();
            BSP_LED_Toggle();
        }
        tud_task();   /* pump the USB device stack */
    }
}

/* -------------------------------------------------------------------------- */
/**
  * @brief  System Clock Configuration
  *         HSE 25MHz -> PLL1 -> sys_ck 480MHz, HCLK 240MHz, PCLK 120MHz
  */
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
    RCC_OscInitStruct.PLL.PLLM = 5;        /* 25MHz /5 = 5MHz   */
    RCC_OscInitStruct.PLL.PLLN = 192;      /* 5MHz *192 = 960MHz VCO */
    RCC_OscInitStruct.PLL.PLLP = 2;        /* 960/2 = 480MHz sys_ck */
    RCC_OscInitStruct.PLL.PLLQ = 4;        /* 960/4 = 240MHz */
    RCC_OscInitStruct.PLL.PLLR = 2;        /* 960/2 = 480MHz */
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
    RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;   /* sys_ck = 480MHz */
    RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;      /* HCLK   = 240MHz */
    RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;     /* PCLK3  = 120MHz */
    RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;     /* PCLK1  = 120MHz */
    RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;     /* PCLK2  = 120MHz */
    RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;     /* PCLK4  = 120MHz */
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_6) != HAL_OK) {
        Error_Handler();
    }

    /* QSPI kernel clock = HCLK (240MHz); prescaler in qspi.c divides to 60MHz */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_QSPI;
    PeriphClkInitStruct.QspiClockSelection = RCC_QSPICLKSOURCE_D1HCLK;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK) {
        Error_Handler();
    }

    HAL_SYSTICK_Config(SystemCoreClock / 1000U);
    HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);
    HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);
}

/* -------------------------------------------------------------------------- */
void Error_Handler(void)
{
    BSP_UART_Printf("!!! Error_Handler triggered !!!\r\n");
    __disable_irq();
    while (1) {
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    BSP_UART_Printf("ASSERT failed: %s:%lu\r\n", file, line);
    while (1) {}
}
#endif
