/* ---------------------------------------------------------------------------
 * Board support - STM32H743ZIT6  (鹿小班 board)
 *
 * Clock strategy - external 25 MHz passive crystal (HSE):
 *
 *   HSE 25 MHz --> /M=2 --> 12.5 MHz --> PLL1 xN=64 --> 800 MHz VCO --> /P=2
 *                                                                   --> SYSCLK 400 MHz
 *   HCLK = SYSCLK/2 = 200 MHz, APB1/2/3/4 = 100 MHz
 *
 *   USB 48 MHz <-- HSI48, auto-trimmed by the CRS against the USB host's
 *                  1 kHz start-of-frame packets (USB needs no crystal).
 * -------------------------------------------------------------------------*/

#include "bsp.h"
#include "stm32h7xx_hal.h"

uint32_t g_sysclk_hz;
uint32_t g_hclk_hz;

/* AF10 is the OTG alternate function on every H7. Some HAL header revisions
 * only expose one of the OTG1/OTG2 spellings, so pin it down locally. */
#ifndef GPIO_AF10_OTG_ANY
#define GPIO_AF10_OTG_ANY  ((uint8_t)0x0A)
#endif
/* PB14/PB15 reach the OTG_HS internal full-speed PHY through AF12. */
#ifndef GPIO_AF12_OTG1_FS
#define GPIO_AF12_OTG1_FS  ((uint8_t)0x0C)
#endif

static void error_trap(void) {
  __disable_irq();
  while (1) { /* hang here - attach the debugger to see who called us */ }
}

/* ------------------------------------------------------------------------ */
static void system_clock_config(void) {
  RCC_OscInitTypeDef       osc  = {0};
  RCC_ClkInitTypeDef       clk  = {0};
  RCC_PeriphCLKInitTypeDef pclk = {0};

  /* 1. Supply configuration. The reset default of PWR_CR3 already selects the
   *    LDO; state it explicitly to match the board hardware. */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /* 2. Voltage scale 1 supports up to 400 MHz on this part. */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) { }

  /* 3. HSE (25 MHz passive crystal, PLL source) + HSI48 (USB).
   *    Leave HSI running: right after reset HSI is the system clock, and HAL
   *    refuses to disable the *current* SYSCLK source, so asking for
   *    HSIState=OFF would make HAL_RCC_OscConfig fail. HSI staying on is
   *    harmless once SYSCLK is switched to the PLL below. */
  osc.OscillatorType      = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_HSI48;
  osc.HSEState            = RCC_HSE_ON;                  /* passive xtal, not bypass */
  osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  osc.HSI48State          = RCC_HSI48_ON;

  osc.PLL.PLLState  = RCC_PLL_ON;
  osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  osc.PLL.PLLM      = 2;                    /* 25 MHz / 2   = 12.5 MHz */
  osc.PLL.PLLN      = 64;                   /* 12.5 * 64    = 800 MHz  */
  osc.PLL.PLLP      = 2;                    /* 800 MHz / 2  = 400 MHz  */
  osc.PLL.PLLQ      = 8;                    /* 100 MHz spare           */
  osc.PLL.PLLR      = 2;
  osc.PLL.PLLRGE    = RCC_PLL1VCIRANGE_3;   /* input is 8..16 MHz      */
  osc.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;      /* VCO 192..836 MHz        */
  osc.PLL.PLLFRACN  = 0;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
    error_trap();
  }

  /* 4. Bus clocks. FLASH_LATENCY_2 is correct for AXI/HCLK = 200 MHz @ VOS1. */
  clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK    |
                  RCC_CLOCKTYPE_D1PCLK1 | RCC_CLOCKTYPE_PCLK1  |
                  RCC_CLOCKTYPE_PCLK2   | RCC_CLOCKTYPE_D3PCLK1;
  clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  clk.SYSCLKDivider  = RCC_SYSCLK_DIV1;   /* CPU  400 MHz */
  clk.AHBCLKDivider  = RCC_HCLK_DIV2;     /* AHB  200 MHz */
  clk.APB3CLKDivider = RCC_APB3_DIV2;     /* 100 MHz */
  clk.APB1CLKDivider = RCC_APB1_DIV2;
  clk.APB2CLKDivider = RCC_APB2_DIV2;
  clk.APB4CLKDivider = RCC_APB4_DIV2;
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) {
    error_trap();
  }

  /* 5. Peripheral clocks sourced from the PLLs:
   *    - USB    <- HSI48 (auto-trimmed against the host's SOF by the CRS)
   *    - SDMMC1 <- PLL1Q (100 MHz) for the SD-card interface            */
  pclk.PeriphClockSelection = RCC_PERIPHCLK_USB | RCC_PERIPHCLK_SDMMC;
  pclk.UsbClockSelection    = RCC_USBCLKSOURCE_HSI48;
  pclk.SdmmcClockSelection  = RCC_SDMMCCLKSOURCE_PLL;   /* PLL1Q = 100 MHz */
  if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) {
    error_trap();
  }

  g_sysclk_hz = HAL_RCC_GetSysClockFreq();
  g_hclk_hz   = HAL_RCC_GetHCLKFreq();
}

/* ---------------------------------------------------------------------------
 * Clock Recovery System: continuously trims HSI48 against the host's SOF so
 * the 48 MHz USB clock keeps the 0.25 % accuracy full-speed USB requires,
 * with no crystal involved.
 * -------------------------------------------------------------------------*/
static void usb_crs_config(void) {
  RCC_CRSInitTypeDef crs = {0};

  __HAL_RCC_CRS_CLK_ENABLE();

  crs.Prescaler = RCC_CRS_SYNC_DIV1;
#if BOARD_TUD_RHPORT == 0
  crs.Source    = RCC_CRS_SYNC_SOURCE_USB2;   /* OTG_FS generates the SOF */
#else
  crs.Source    = RCC_CRS_SYNC_SOURCE_USB1;   /* OTG_HS generates the SOF */
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

void board_led_toggle(void) {
  HAL_GPIO_TogglePin(LED_GPIO_PORT, LED_GPIO_PIN);
}

uint32_t board_millis(void)          { return HAL_GetTick(); }
void     board_delay_ms(uint32_t ms) { HAL_Delay(ms); }

/* ---------------------------------------------------------------------------
 * USB pins, clock and power.
 * TinyUSB itself takes care of the GCCFG power-down bit and the core reset;
 * everything below is what the stack expects the board to have done already.
 * -------------------------------------------------------------------------*/
static void usb_hw_init(void) {
  GPIO_InitTypeDef g = {0};

#if BOARD_TUD_RHPORT == 0
  /* USB2_OTG_FS - PA11 = DM, PA12 = DP */
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
#else
  /* USB1_OTG_HS driven by its internal full-speed PHY - PB14 = DM, PB15 = DP */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  g.Pin       = GPIO_PIN_14 | GPIO_PIN_15;
  g.Mode      = GPIO_MODE_AF_PP;
  g.Pull      = GPIO_NOPULL;
  g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  g.Alternate = GPIO_AF12_OTG1_FS;
  HAL_GPIO_Init(GPIOB, &g);

  __HAL_RCC_USB1_OTG_HS_CLK_ENABLE();
#endif

  /* Tell the USB transceiver that its dedicated 3.3 V rail is valid. */
  HAL_PWREx_EnableUSBVoltageDetector();

  /* The USB interrupt must not outrank anything that could preempt the
   * stack's own critical sections. Priority 5 leaves room either way. */
#if BOARD_TUD_RHPORT == 0
  HAL_NVIC_SetPriority(OTG_FS_IRQn, 5, 0);
#else
  HAL_NVIC_SetPriority(OTG_HS_IRQn, 5, 0);
#endif
}

/* ------------------------------------------------------------------------ */
void bsp_init(void) {
  /* Instruction + data cache. Safe here because the DWC2 core runs in FIFO
   * (slave) mode: every USB byte moves through CPU accesses to the peripheral
   * region, which the default MPU map already treats as non-cacheable. */
  SCB_EnableICache();
  SCB_EnableDCache();

  HAL_Init();                 /* 4-bit preemption priority grouping + SysTick */
  system_clock_config();
  usb_crs_config();

  led_init();
  usb_hw_init();
}
