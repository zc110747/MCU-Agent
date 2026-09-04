/**
  ******************************************************************************
  * @file    main.c
  * @brief   SD card JPEG slideshow on a 240x240 SPI OLED (ST7789).
  *
  * Board : LXB743ZI-P1 core board, STM32H743ZIT6 @ 480MHz
  * Flow  : SDMMC1 -> FatFs -> TJpgDec -> crop + scale -> SPI6 -> OLED
  ******************************************************************************
  */

#include "main.h"

#include "bsp_log.h"
#include "drv_sdio.h"
#include "bsp_oled.h"
#include "app_slideshow.h"

/* Private variables ---------------------------------------------------------*/
SD_HandleTypeDef   hsd1;
SPI_HandleTypeDef  hspi6;
UART_HandleTypeDef huart1;

/* Private function prototypes -----------------------------------------------*/
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI6_Init(void);
static void MX_SDMMC1_SD_Init(void);

/**
  * @brief  The application entry point.
  */
int main(void)
{
    /* MPU / cache must be set up before anything touches RAM heavily */
    MPU_Config();
    SCB_EnableICache();
    SCB_EnableDCache();

    /* Reset of all peripherals, init the Flash interface and the time base */
    HAL_Init();
    __enable_irq();

    /* Configure the system clock: HSE 25MHz -> PLL1 -> SYSCLK 480MHz */
    SystemClock_Config();

    /* Initialise all configured peripherals */
    MX_GPIO_Init();
    MX_SPI6_Init();
    MX_SDMMC1_SD_Init();

    /* Console first, so that every later stage can report progress */
    bsp_log_init();

    printf_log("\r\n\r\n");
    PRINT_LOG("[I] ==============================================\r\n");
    PRINT_LOG("[I]  STM32H743 SD-card JPEG slideshow\r\n");
    PRINT_LOG("[I]  SYSCLK    : %lu Hz\r\n", (unsigned long)HAL_RCC_GetSysClockFreq());
    PRINT_LOG("[I]  Build     : %s %s\r\n", __DATE__, __TIME__);
    PRINT_LOG("[I] ==============================================\r\n");

    /* Panel up first: gives immediate visual feedback that the board booted */
    bsp_oled_init();

    /* Storage */
    if (bsp_sdcard_mount() != RT_OK) {
        PRINT_LOG("[E] SD card mount failed, halting slideshow\r\n");
        bsp_oled_show_banner("SD CARD ERROR", "check card / wiring");
    }

    /* Slideshow: scans 0:/image, decodes and pushes one frame every 5s */
    app_slideshow_init();

    while (1) {
        app_slideshow_poll();
        HAL_Delay(10);
    }
}

/**
  * @brief System Clock Configuration.
  *        HSE = 25MHz, PLL1: /5 *192 /2 -> 480MHz SYSCLK
  *        HCLK 240MHz, APB1/2/3/4 120MHz
  */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /* Supply configuration update enable */
    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

    /* Configure the main internal regulator output voltage */
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {
    }

    RCC_OscInitStruct.OscillatorType   = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState         = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState     = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource    = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM         = 5;
    RCC_OscInitStruct.PLL.PLLN         = 192;
    RCC_OscInitStruct.PLL.PLLP         = 2;
    RCC_OscInitStruct.PLL.PLLQ         = 4;
    RCC_OscInitStruct.PLL.PLLR         = 2;
    RCC_OscInitStruct.PLL.PLLRGE       = RCC_PLL1VCIRANGE_2;
    RCC_OscInitStruct.PLL.PLLVCOSEL    = RCC_PLL1VCOWIDE;
    RCC_OscInitStruct.PLL.PLLFRACN     = 0;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2
                                     | RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.SYSCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
    RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) {
        Error_Handler();
    }
}

/**
  * @brief SDMMC1 Initialization Function (4-bit bus, ~25MHz card clock).
  */
static void MX_SDMMC1_SD_Init(void)
{
    hsd1.Instance                 = SDMMC1;
    hsd1.Init.ClockEdge           = SDMMC_CLOCK_EDGE_RISING;
    hsd1.Init.ClockPowerSave      = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    hsd1.Init.BusWide             = SDMMC_BUS_WIDE_4B;
    hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
    hsd1.Init.ClockDiv            = 6;

    if (HAL_SD_Init(&hsd1) != HAL_OK) {
        /* No card inserted is not fatal: the app reports it on screen. */
        return;
    }
}

/**
  * @brief SPI6 Initialization Function.
  *        1-line (MOSI only) master, 8-bit, 60MHz (D3PCLK1 120MHz / 2).
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

    if (HAL_SPI_Init(&hspi6) != HAL_OK) {
        Error_Handler();
    }
}

/**
  * @brief GPIO Initialization Function (LED, LCD_BL, LCD_DC).
  */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOG, LED_Pin | LCD_BL_Pin | LCD_DC_Pin, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin   = LED_Pin | LCD_BL_Pin | LCD_DC_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);
}

/**
  * @brief MPU Configuration.
  *
  * Only the background map is used. SDMMC transfers run in CPU polling mode
  * (HAL_SD_ReadBlocks writes byte-by-byte from the FIFO), so no cache
  * maintenance or non-cacheable DMA window is required here.
  */
static void MPU_Config(void)
{
    HAL_MPU_Disable();
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

/**
  * @brief  TIM13 drives the HAL time base (see stm32h7xx_hal_timebase_tim.c).
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM13) {
        HAL_IncTick();
    }
}

/**
  * @brief  This function is executed in case of error occurrence.
  */
void Error_Handler(void)
{
    __disable_irq();
    while (1) {
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    printf_log("assert failed: %s:%lu\r\n", (char *)file, (unsigned long)line);
}
#endif /* USE_FULL_ASSERT */
