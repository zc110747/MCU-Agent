/**
  ******************************************************************************
  * @file    app/main.c
  * @brief   STM32H743ZIT6 QSPI-U-Disk bootloader (upgrade + secure jump).
  *
  * Boot flow:
  *   1. HAL / clock / MPU (all regions non-cacheable for safe flash writes)
  *   2. UART + QSPI + LED init
  *   3. relocate the internal-flash write engine into AXI SRAM (BFLASH_Relocate)
  *   4. BSP_Upgrade_Check(): mount QSPI FAT, if a package (stm32h7_xx.bin +
  *      verify.json) is present and passes HMAC/version checks, erase the app
  *      region and program the new image, then update the system config and
  *      unmount (TinyUSB MSC takes over the raw QSPI for host access)
  *   5. BSP_Boot_Enter(): validate the app image; if OK, an 8 s window lets
  *      the user plug in USB to enter U-disk mode, otherwise reset HW + jump
  *
  * LED: 200 ms fast blink while the bootloader is alive (override to 1000 ms
  * in the test app for a 1 Hz heartbeat).
  ******************************************************************************
  */
#include "stm32h7xx_hal.h"
#include "uart.h"
#include "qspi.h"
#include "led.h"
#include "mpu.h"
#include "flash_upgrade.h"
#include "upgrade.h"
#include "boot.h"

static void SystemClock_Config(void);
static void Error_Handler(void);

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    /* All bootloader-visible memory is non-cacheable (see bsp/mpu.c) so flash
       writes and QSPI/USB DMA never see stale cache lines. */
    BSP_MPU_Init();

    if (BSP_UART_Init() != HAL_OK) {
        Error_Handler();
    }

    BSP_UART_Printf("\r\n");
    BSP_UART_Printf("========================================================\r\n");
    BSP_UART_Printf(" STM32H743 Bootloader  (QSPI U-Disk + secure upgrade)\r\n");
    BSP_UART_Printf("========================================================\r\n");
    BSP_UART_Printf(" CPU Clock : %lu MHz\r\n", (unsigned long)(SystemCoreClock / 1000000UL));

    if (BSP_QSPI_Init() != QSPI_OK) {
        BSP_UART_Printf(" QSPI init FAILED\r\n");
        Error_Handler();
    }
    BSP_UART_Printf(" QSPI initialised (HAL indirect mode)\r\n");

    BSP_LED_Init();
    BSP_UART_Printf(" LED fast blink started (PG7, 200 ms)\r\n");

    /* Copy the flash-write engine from FLASH into AXI SRAM before any
       erase/program call (the CPU must not fetch from bank1 while bank1 is being
       written, and must not execute from DTCM which is data-only on Cortex-M7). */
    BSP_UART_Printf("[BOOT] relocating flash engine to AXI SRAM...\r\n");
    BFLASH_Relocate();

    /* 1. Process any upgrade package on the QSPI U-disk. */
    BSP_UART_Printf("[BOOT] checking QSPI volume for upgrade package...\r\n");
    BSP_Upgrade_Check();

    /* 2. Validate app and either count down to a jump or stay in U-disk mode. */
    BSP_Boot_Enter();

    /* never returns */
    while (1) {
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
