/**
  ******************************************************************************
  * @file    main.c
  * @brief   STM32H743ZIT6 - OLED Chinese text demo (bare metal, no RTOS).
  *
  *  Boot sequence:
  *    MPU -> I-Cache -> HAL_Init -> 480 MHz clock -> GPIO/SPI6/SDMMC1/USART1
  *    -> application_init() -> application_run() forever.
  *
 *  Cache policy
 *  ------------
 *  Both I-Cache and D-Cache are enabled.  SDMMC1 currently uses the polling
 *  HAL_SD_ReadBlocks()/WriteBlocks() path (the CPU drains the SDMMC FIFO into
 *  the sector buffer), so no SDMMC internal DMA touches RAM and there is no
 *  cache-coherency hazard - D-Cache stays on safely.  If SDMMC IDMA (or any
 *  other peripheral DMA) is introduced later, the DMA buffer regions must be
 *  made non-cacheable via the MPU, NOT by disabling D-Cache globally.
 *  (See MPU_Config() for the region setup.)
  ******************************************************************************
  */
#include "main.h"
#include "app_main.h"
#include "log.h"

/* Peripheral handles --------------------------------------------------------*/
SPI_HandleTypeDef  hspi6;
UART_HandleTypeDef huart1;
SD_HandleTypeDef   hsd1;

/* Which oscillator actually ended up driving PLL1 (see SystemClock_Config) */
volatile ClockSource_t g_clock_source  = CLOCK_SRC_HSE_XTAL;
/* Set to 1 by the CSS NMI if the 25 MHz crystal dies while running */
volatile uint8_t       g_hse_css_fault = 0U;

/* Private prototypes --------------------------------------------------------*/
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI6_Init(void);
static void MX_SDMMC1_SD_Init(void);
static void MX_USART1_UART_Init(void);

/**
  * @brief  Application entry point.
  */
int main(void)
{
    /* 1. MPU + caches --------------------------------------------------------*/
    MPU_Config();
    SCB_EnableICache();
    SCB_EnableDCache();
    
    /* 2. HAL / SysTick -------------------------------------------------------*/
    HAL_Init();

    /* 3. 480 MHz system clock ------------------------------------------------*/
    SystemClock_Config();

    /* 4. Peripherals ---------------------------------------------------------*/
    MX_GPIO_Init();
    MX_USART1_UART_Init();
    log_uart_init();
    MX_SPI6_Init();
    MX_SDMMC1_SD_Init();

    /* 5. Application ---------------------------------------------------------*/
    application_init();

    while (1)
    {
        application_run();
    }
}

/**
  * @brief  System Clock Configuration.
  *
  * Oscillator: 25 MHz PASSIVE crystal wired to OSC_IN (PH0) / OSC_OUT (PH1).
  *   -> RCC_HSE_ON drives the on-chip Pierce oscillator against the crystal.
  *   -> RCC_HSE_BYPASS must NOT be used: that mode expects an active
  *      oscillator/TCXO feeding OSC_IN, and would leave HSE dead here.
  *
  *   HSE  = 25 MHz
  *   PLL1 : M=5 -> 5 MHz ref ; N=192 -> 960 MHz VCO ; P=2 -> SYSCLK 480 MHz
  *   HCLK = 240 MHz, APBx = 120 MHz
  *
  * A crystal can fail to start (bad load caps, cold joint, damaged part).
  * Instead of dying in Error_Handler(), we fall back to the internal HSI so
  * the UART still comes up and can report the fault.
  */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /* Supply configuration: internal LDO */
    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

    /* VOS0 is required to reach 480 MHz */
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
    {
    }

    /* ---- Attempt 1: 25 MHz passive crystal --------------------------------*/
    RCC_OscInitStruct.OscillatorType   = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState         = RCC_HSE_ON;   /* crystal, not bypass */
    RCC_OscInitStruct.PLL.PLLState     = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource    = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM         = 5;            /* 25 MHz / 5  = 5 MHz */
    RCC_OscInitStruct.PLL.PLLN         = 192;          /* 5 MHz * 192 = 960 MHz */
    RCC_OscInitStruct.PLL.PLLP         = 2;            /* 960 / 2     = 480 MHz */
    RCC_OscInitStruct.PLL.PLLQ         = 4;
    RCC_OscInitStruct.PLL.PLLR         = 2;
    RCC_OscInitStruct.PLL.PLLRGE       = RCC_PLL1VCIRANGE_2;  /* ref 4-8 MHz */
    RCC_OscInitStruct.PLL.PLLVCOSEL    = RCC_PLL1VCOWIDE;
    RCC_OscInitStruct.PLL.PLLFRACN     = 0;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) == HAL_OK)
    {
        g_clock_source = CLOCK_SRC_HSE_XTAL;
    }
    else
    {
        /* ---- Attempt 2: crystal never oscillated, run from HSI ------------*/
        g_clock_source = CLOCK_SRC_HSI_FALLBACK;

        /* Include HSE in the type mask so the dead oscillator is really
         * switched off instead of being left enabled but unused. */
        RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI |
                                                RCC_OSCILLATORTYPE_HSE;
        RCC_OscInitStruct.HSEState            = RCC_HSE_OFF;
        RCC_OscInitStruct.HSIState            = RCC_HSI_DIV1;   /* 64 MHz */
        RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
        RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
        RCC_OscInitStruct.PLL.PLLM            = 16;   /* 64 MHz / 16 = 4 MHz  */
        RCC_OscInitStruct.PLL.PLLN            = 240;  /* 4 * 240   = 960 MHz  */
        /* P/Q/R/RGE/VCOSEL unchanged -> still 480 MHz SYSCLK */

        if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
        {
            Error_Handler();
        }
    }

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                       RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
                                       RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.SYSCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
    RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
    {
        Error_Handler();
    }

    /* Arm the Clock Security System once the crystal is confirmed running.
     * If the crystal later dies, hardware disables HSE, switches SYSCLK back
     * to HSI and raises an NMI -> HAL_RCC_CSSCallback() below.
     * Pointless (and not allowed) when we are already on HSI. */
    if (g_clock_source == CLOCK_SRC_HSE_XTAL)
    {
        HAL_RCC_EnableCSS();
    }
}

/**
  * @brief  HSE clock-failure callback, reached from NMI via
  *         HAL_RCC_NMI_IRQHandler().
  *
  * Keep this tiny: SysTick cannot preempt NMI, so any HAL call that polls
  * HAL_GetTick() (HAL_RCC_OscConfig, HAL_Delay, ...) would hang forever here.
  * We only latch the event; recovery is reported from the main loop.
  */
void HAL_RCC_CSSCallback(void)
{
    g_clock_source  = CLOCK_SRC_HSI_CSS_RESCUE;
    g_hse_css_fault = 1U;
    SystemCoreClockUpdate();   /* SYSCLK has been forced to HSI by hardware */
}

/**
  * @brief  MPU configuration.
  *
  * Region 0 : AXI-SRAM (0x24000000, 512 KB) marked normal / non-shareable.
  *            This is where .data/.bss/heap/stack and every SD buffer live.
  */
static void MPU_Config(void)
{
    MPU_Region_InitTypeDef MPU_InitStruct = {0};

    HAL_MPU_Disable();

    MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
    MPU_InitStruct.Number           = MPU_REGION_NUMBER0;
    MPU_InitStruct.BaseAddress      = 0x24000000;
    MPU_InitStruct.Size             = MPU_REGION_SIZE_512KB;
    MPU_InitStruct.SubRegionDisable = 0x00;
    MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL0;
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
    MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
    MPU_InitStruct.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
    MPU_InitStruct.IsCacheable      = MPU_ACCESS_CACHEABLE;
    MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

/**
  * @brief  GPIO init: LED (PG7), LCD_BL (PG12), LCD_DC (PG15).
  */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    /* Start with LED off, backlight off, DC low */
    HAL_GPIO_WritePin(GPIOG, LED_Pin | LCD_BL_Pin | LCD_DC_Pin, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin   = LED_Pin | LCD_BL_Pin | LCD_DC_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);
}

/**
  * @brief  SPI6 init - display link.
  *
  * Half duplex TX only (1 line): the panel has no MISO.
  * NSS is driven by hardware (PG8) with a pulse between frames.
  */
static void MX_SPI6_Init(void)
{
    hspi6.Instance                        = SPI6;
    hspi6.Init.Mode                       = SPI_MODE_MASTER;
    hspi6.Init.Direction                  = SPI_DIRECTION_1LINE;
    hspi6.Init.DataSize                   = SPI_DATASIZE_8BIT;
    hspi6.Init.CLKPolarity                = SPI_POLARITY_LOW;
    hspi6.Init.CLKPhase                   = SPI_PHASE_1EDGE;
    hspi6.Init.NSS                        = SPI_NSS_HARD_OUTPUT;
    hspi6.Init.BaudRatePrescaler          = SPI_BAUDRATEPRESCALER_2;
    hspi6.Init.FirstBit                   = SPI_FIRSTBIT_MSB;
    hspi6.Init.TIMode                     = SPI_TIMODE_DISABLE;
    hspi6.Init.CRCCalculation             = SPI_CRCCALCULATION_DISABLE;
    hspi6.Init.CRCPolynomial              = 0x0;
    hspi6.Init.NSSPMode                   = SPI_NSS_PULSE_ENABLE;
    hspi6.Init.NSSPolarity                = SPI_NSS_POLARITY_LOW;
    hspi6.Init.FifoThreshold              = SPI_FIFO_THRESHOLD_02DATA;
    hspi6.Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    hspi6.Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    hspi6.Init.MasterSSIdleness           = SPI_MASTER_SS_IDLENESS_00CYCLE;
    hspi6.Init.MasterInterDataIdleness    = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
    hspi6.Init.MasterReceiverAutoSusp     = SPI_MASTER_RX_AUTOSUSP_DISABLE;
    hspi6.Init.MasterKeepIOState          = SPI_MASTER_KEEP_IO_STATE_DISABLE;
    hspi6.Init.IOSwap                     = SPI_IO_SWAP_DISABLE;

    if (HAL_SPI_Init(&hspi6) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief  SDMMC1 init - 4 bit bus, holds the Chinese font files.
  * @note   A missing card must not brick the boot, so a failure here is
  *         reported by the application instead of trapping in Error_Handler().
  */
static void MX_SDMMC1_SD_Init(void)
{
    hsd1.Instance                 = SDMMC1;
    hsd1.Init.ClockEdge           = SDMMC_CLOCK_EDGE_RISING;
    hsd1.Init.ClockPowerSave      = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    hsd1.Init.BusWide             = SDMMC_BUS_WIDE_4B;
    hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
    hsd1.Init.ClockDiv            = 4;

    (void)HAL_SD_Init(&hsd1);
}

/**
  * @brief  USART1 init - debug console on PA9 / PA10, 115200-8-N-1.
  */
static void MX_USART1_UART_Init(void)
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

    if (HAL_UART_Init(&huart1) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief  Unrecoverable error: blink the LED fast so the board shows it.
  */
void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
}
#endif
