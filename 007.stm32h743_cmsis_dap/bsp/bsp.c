/* ---------------------------------------------------------------------------
 * Board support - STM32H743ZIT6, CMSIS-DAP v1 probe
 *
 * Clock strategy - external 25 MHz passive crystal (HSE):
 *
 *   HSE 25 MHz --> /M=2 --> 12.5 MHz --> PLL1 xN=64 --> 800 MHz VCO --> /P=2
 *                                                                   --> SYSCLK 400 MHz
 *   HCLK = SYSCLK/2 = 200 MHz, APB1/2/3/4 = 100 MHz
 *
 *   USB 48 MHz <-- HSI48, auto-trimmed by the CRS against the USB host's
 *                  1 kHz start-of-frame packets (no crystal path needed).
 *
 * SYSCLK is pinned at 400 MHz because DAP_config.h hard-codes CPU_CLOCK to the
 * same number: the SWCLK bit-banging delay is derived from it, so the two must
 * never drift apart.
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
   *    refuses to disable the *current* SYSCLK source. */
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

  /* 5. USB <- HSI48 (auto-trimmed against the host's SOF by the CRS). */
  pclk.PeriphClockSelection = RCC_PERIPHCLK_USB;
  pclk.UsbClockSelection    = RCC_USBCLKSOURCE_HSI48;
  if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) {
    error_trap();
  }

  g_sysclk_hz = HAL_RCC_GetSysClockFreq();
  g_hclk_hz   = HAL_RCC_GetHCLKFreq();
}

/* ---------------------------------------------------------------------------
 * Clock Recovery System: continuously trims HSI48 against the host's SOF so
 * the 48 MHz USB clock keeps the 0.25 % accuracy full-speed USB requires.
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
uint32_t board_millis(void)          { return HAL_GetTick(); }
void     board_delay_ms(uint32_t ms) { HAL_Delay(ms); }

/* ---------------------------------------------------------------------------
 * 96-bit unique device ID -> 24 hex characters.
 * Used as the USB iSerialNumber so several probes can coexist on one host and
 * OpenOCD's `cmsis-dap serial ...` can pick between them.
 * -------------------------------------------------------------------------*/
void board_get_unique_id(char *out, uint32_t out_size) {
  static const char hex[] = "0123456789ABCDEF";
  const uint32_t words[3] = {
    HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2()
  };
  uint32_t n = 0;

  for (uint32_t w = 0; w < 3U; w++) {
    for (int32_t nib = 7; nib >= 0; nib--) {
      if (n + 1U >= out_size) { goto done; }
      out[n++] = hex[(words[w] >> (nib * 4)) & 0xFU];
    }
  }
done:
  out[n] = '\0';
}

/* ---------------------------------------------------------------------------
 * USB transceiver power (VDD33USB)
 *
 * The OTG_FS / OTG_HS embedded PHY is supplied from the VDD33USB domain. That
 * domain is fed either by an external 3.3 V rail or by the on-chip LDO
 * (USBREGEN). Whichever source is used, the USB voltage detector (USB33DEN)
 * must be enabled and the USB33RDY flag polled BEFORE the core is touched -
 * otherwise the transceiver is unpowered and the host never sees the device.
 * This is the single most common reason a freshly-flashed H7 "shows no USB".
 * -------------------------------------------------------------------------*/
static void usb_power_init(void) {
#if DAP_USB_INTERNAL_REGULATOR
  /* No external 3.3 V on VDD33USB -> generate it from VDD via the on-chip
   * LDO. Leave DAP_USB_INTERNAL_REGULATOR=OFF (default) when the board wires
   * 3.3 V to VDD33USB; enabling the LDO then fights the external source and
   * (on this board) the USB33RDY flag never comes up. */
  PWR->CR3 |= PWR_CR3_USBREGEN;
#endif

  /* Enable the USB voltage detector. With an external VDD33USB this is what
   * makes USB33RDY come up; with the internal LDO it gates the regulator. */
  HAL_PWREx_EnableUSBVoltageDetector();

  /* Wait (BOUNDED) for VDD33USB to read valid. Bounded because a mis-wired
   * board - or one whose VDD33USB is already externally powered and so never
   * drives the internal-LDO ready flag - must not wedge the firmware here
   * forever; the transceiver is frequently already powered regardless, and
   * TinyUSB can bring the link up without this flag. 100 ms is far longer
   * than a healthy regulator/detector needs. */
  uint32_t const t0 = HAL_GetTick();
  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_USB33RDY)) {
    if ((HAL_GetTick() - t0) > 100U) {
      break;   /* timed out: proceed anyway and let the stack try */
    }
  }
}

/* ---------------------------------------------------------------------------
 * Early proof-of-life: three short blinks on the status LED (PG7) the moment
 * the firmware reaches bsp_init(). If the LED never blinks, the chip is not
 * booting (clock / HSE failure) - a different problem from a silent USB link.
 * -------------------------------------------------------------------------*/
static void boot_heartbeat(void) {
  __HAL_RCC_GPIOG_CLK_ENABLE();
  GPIO_InitTypeDef g = {0};
  g.Pin   = GPIO_PIN_7;
  g.Mode  = GPIO_MODE_OUTPUT_PP;
  g.Pull  = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOG, &g);

  for (uint32_t i = 0; i < 3U; i++) {
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_7, GPIO_PIN_SET);
    HAL_Delay(80);
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_7, GPIO_PIN_RESET);
    HAL_Delay(120);
  }
}

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

  /* Priority 5: below anything that could preempt the stack's own critical
   * sections, above nothing that matters here. TinyUSB enables the IRQ in
   * tud_init(); we enable it here too so the line is armed regardless. */
#if BOARD_TUD_RHPORT == 0
  HAL_NVIC_SetPriority(OTG_FS_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
#else
  HAL_NVIC_SetPriority(OTG_HS_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(OTG_HS_IRQn);
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
  boot_heartbeat();           /* proof-of-life on PG7 before anything else */
  usb_power_init();           /* VDD33USB regulator + detector + USB33RDY */
  usb_crs_config();

  usb_hw_init();
}
