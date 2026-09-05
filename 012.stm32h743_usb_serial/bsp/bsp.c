/* ---------------------------------------------------------------------------
 * Board support - STM32H743ZIT6, external 25 MHz crystal
 *
 *   HSE 25 MHz -> /M=2 -> 12.5 MHz -> PLL1 xN=64 -> 800 MHz VCO -> /P=2
 *                                                              -> SYSCLK 400 MHz
 *   HCLK = SYSCLK/2 = 200 MHz, APB1/2/3/4 = 100 MHz  (UART4 kernel = 100 MHz)
 *
 *   USB 48 MHz <- HSI48, auto-trimmed by the CRS against the host's 1 kHz
 *                 start-of-frame packets.
 *
 * Cache policy: D-cache stays ON (no MPU carve-outs). DMA buffers live in
 * AXI SRAM and are bracketed with explicit Clean/Invalidate-by-address calls
 * in app/uart_bridge.c.
 * -------------------------------------------------------------------------*/

#include "bsp.h"
#include "stm32h7xx_hal.h"

uint32_t g_sysclk_hz;
uint32_t g_hclk_hz;
uint32_t g_pclk1_hz;

#ifndef GPIO_AF10_OTG_ANY
#define GPIO_AF10_OTG_ANY  ((uint8_t) 0x0A)
#endif

static void error_trap(void) {
  __disable_irq();
  while (1) { }        /* hang here - attach the debugger to see the caller */
}

/* ------------------------------------------------------------------------ */
static void system_clock_config(void) {
  RCC_OscInitTypeDef       osc  = {0};
  RCC_ClkInitTypeDef       clk  = {0};
  RCC_PeriphCLKInitTypeDef pclk = {0};

  /* 1. LDO supply (reset default, stated explicitly to match the hardware) */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /* 2. VOS1 supports up to 400 MHz */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) { }

  /* 3. HSE (25 MHz passive crystal) + HSI48 for USB.
   *    HSI is left on: right after reset it is the system clock, and HAL
   *    refuses to disable the current SYSCLK source. */
  osc.OscillatorType      = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_HSI48;
  osc.HSEState            = RCC_HSE_ON;
  osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  osc.HSI48State          = RCC_HSI48_ON;

  osc.PLL.PLLState  = RCC_PLL_ON;
  osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  osc.PLL.PLLM      = 2;                    /* 25 MHz / 2  = 12.5 MHz */
  osc.PLL.PLLN      = 64;                   /* 12.5 * 64   = 800 MHz  */
  osc.PLL.PLLP      = 2;                    /* 800 / 2     = 400 MHz  */
  osc.PLL.PLLQ      = 8;                    /* 100 MHz spare          */
  osc.PLL.PLLR      = 2;
  osc.PLL.PLLRGE    = RCC_PLL1VCIRANGE_3;   /* input 8..16 MHz        */
  osc.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;      /* VCO 192..836 MHz       */
  osc.PLL.PLLFRACN  = 0;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) error_trap();

  /* 4. Bus clocks */
  clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK    |
                  RCC_CLOCKTYPE_D1PCLK1 | RCC_CLOCKTYPE_PCLK1  |
                  RCC_CLOCKTYPE_PCLK2   | RCC_CLOCKTYPE_D3PCLK1;
  clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  clk.SYSCLKDivider  = RCC_SYSCLK_DIV1;   /* CPU 400 MHz */
  clk.AHBCLKDivider  = RCC_HCLK_DIV2;     /* AHB 200 MHz */
  clk.APB3CLKDivider = RCC_APB3_DIV2;     /* 100 MHz     */
  clk.APB1CLKDivider = RCC_APB1_DIV2;
  clk.APB2CLKDivider = RCC_APB2_DIV2;
  clk.APB4CLKDivider = RCC_APB4_DIV2;
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) error_trap();

  /* 5. USB <- HSI48. UART4 keeps the reset default (APB1 / D2PCLK1). */
  pclk.PeriphClockSelection = RCC_PERIPHCLK_USB;
  pclk.UsbClockSelection    = RCC_USBCLKSOURCE_HSI48;
  if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) error_trap();

  g_sysclk_hz = HAL_RCC_GetSysClockFreq();
  g_hclk_hz   = HAL_RCC_GetHCLKFreq();
  g_pclk1_hz  = HAL_RCC_GetPCLK1Freq();
}

/* ---------------------------------------------------------------------------
 * Clock Recovery System - trims HSI48 against the host SOF so the 48 MHz USB
 * clock keeps the 0.25 % accuracy full-speed USB needs.
 * -------------------------------------------------------------------------*/
static void usb_crs_config(void) {
  RCC_CRSInitTypeDef crs = {0};

  __HAL_RCC_CRS_CLK_ENABLE();

  crs.Prescaler             = RCC_CRS_SYNC_DIV1;
#if BOARD_TUD_RHPORT == 0
  crs.Source                = RCC_CRS_SYNC_SOURCE_USB2;
#else
  crs.Source                = RCC_CRS_SYNC_SOURCE_USB1;
#endif
  crs.Polarity              = RCC_CRS_SYNC_POLARITY_RISING;
  crs.ReloadValue           = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000U, 1000U);
  crs.ErrorLimitValue       = RCC_CRS_ERRORLIMIT_DEFAULT;
  crs.HSI48CalibrationValue = RCC_CRS_HSI48CALIBRATION_DEFAULT;

  HAL_RCCEx_CRSConfig(&crs);
}

/* ------------------------------------------------------------------------ */
static void led_init(void) {
  GPIO_InitTypeDef g = {0};
  LED_GPIO_CLK_EN();
  g.Pin   = LED_GPIO_PIN;
  g.Mode  = GPIO_MODE_OUTPUT_PP;
  g.Pull  = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_PORT, &g);
  board_led_write(false);
}

void board_led_write(bool on) {
#if LED_ACTIVE_HIGH
  HAL_GPIO_WritePin(LED_GPIO_PORT, LED_GPIO_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
#else
  HAL_GPIO_WritePin(LED_GPIO_PORT, LED_GPIO_PIN, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
#endif
}

void board_led_toggle(void) { HAL_GPIO_TogglePin(LED_GPIO_PORT, LED_GPIO_PIN); }

uint32_t board_millis(void)          { return HAL_GetTick(); }
void     board_delay_ms(uint32_t ms) { HAL_Delay(ms); }

/* ------------------------------------------------------------------------ */
void board_usb_init(void) {
  GPIO_InitTypeDef g = {0};

  /* USB2_OTG_FS: PA11 = DM, PA12 = DP */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  g.Pin       = GPIO_PIN_11 | GPIO_PIN_12;
  g.Mode      = GPIO_MODE_AF_PP;
  g.Pull      = GPIO_NOPULL;
  g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  g.Alternate = GPIO_AF10_OTG_ANY;
  HAL_GPIO_Init(GPIOA, &g);

#if OTG_FS_VBUS_SENSE
  g.Pin  = GPIO_PIN_9;
  g.Mode = GPIO_MODE_INPUT;
  g.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &g);
#endif

  __HAL_RCC_USB2_OTG_FS_CLK_ENABLE();

  /* Tell the transceiver its dedicated 3.3 V rail is valid. */
  HAL_PWREx_EnableUSBVoltageDetector();

  /* Same priority as the UART/DMA interrupts: none of them preempts another. */
  HAL_NVIC_SetPriority(OTG_FS_IRQn, 5u, 0u);
}

/* ------------------------------------------------------------------------ */
void bsp_init(void) {
  SCB_EnableICache();
  SCB_EnableDCache();

  HAL_Init();                  /* priority grouping + 1 ms SysTick timebase */
  system_clock_config();
  usb_crs_config();

  led_init();
}
