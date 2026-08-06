/**
 ******************************************************************************
 * @file    bsp_board.c
 * @brief   Clock tree, MPU, cache and run LED for the STM32H743ZIT6 board.
 ******************************************************************************
 */

#include "bsp_board.h"

/* ==========================================================================
 * MPU
 *
 * The DCMI DMA writes the camera frames into AXI SRAM (0x24000000).  Marking
 * that region as Normal / non-cacheable removes the need for any manual cache
 * maintenance when the CPU hands the buffer over to the USB stack.
 * ========================================================================== */
void bsp_mpu_config(void)
{
  MPU_Region_InitTypeDef mpu = {0};

  HAL_MPU_Disable();

  /* Region 0: whole AXI SRAM (512 KB) - Normal, non-cacheable, shareable */
  mpu.Enable           = MPU_REGION_ENABLE;
  mpu.Number           = MPU_REGION_NUMBER0;
  mpu.BaseAddress      = FRAMEBUF_BASE_ADDR;
  mpu.Size             = MPU_REGION_SIZE_512KB;
  mpu.SubRegionDisable = 0x00;
  mpu.TypeExtField     = MPU_TEX_LEVEL1;              /* TEX=1,C=0,B=0 => Normal NC */
  mpu.AccessPermission = MPU_REGION_FULL_ACCESS;
  mpu.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
  mpu.IsShareable      = MPU_ACCESS_SHAREABLE;
  mpu.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
  mpu.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
  HAL_MPU_ConfigRegion(&mpu);

  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

void bsp_cache_enable(void)
{
  SCB_EnableICache();
  SCB_EnableDCache();
}

/* ==========================================================================
 * Clock tree
 *
 *   HSE            = 25 MHz (external crystal)
 *   PLL1 : M=5  -> 5 MHz reference
 *          N=192 -> 960 MHz VCO
 *          P=2  -> 480 MHz SYSCLK
 *          Q=20 ->  48 MHz USB OTG FS clock
 *   HCLK  = SYSCLK / 2 = 240 MHz
 *   APBx  = HCLK   / 2 = 120 MHz
 * ========================================================================== */
void bsp_clock_config(void)
{
  RCC_OscInitTypeDef        osc  = {0};
  RCC_ClkInitTypeDef        clk  = {0};
  RCC_PeriphCLKInitTypeDef  perf = {0};

  /* The 480 MHz operating point requires VOS0 with the internal LDO. */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {
    /* wait for the regulator to settle */
  }

  osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  osc.HSEState       = RCC_HSE_ON;          /* external crystal + internal oscillator */
  osc.PLL.PLLState   = RCC_PLL_ON;
  osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
  osc.PLL.PLLM       = 5;
  osc.PLL.PLLN       = 192;
  osc.PLL.PLLP       = 2;
  osc.PLL.PLLQ       = 20;                  /* 960 / 20 = 48 MHz for USB */
  osc.PLL.PLLR       = 2;
  osc.PLL.PLLRGE     = RCC_PLL1VCIRANGE_2;  /* 4 - 8 MHz input range */
  osc.PLL.PLLVCOSEL  = RCC_PLL1VCOWIDE;     /* 192 - 960 MHz VCO */
  osc.PLL.PLLFRACN   = 0;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
    Error_Handler();
  }

  clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK    |
                  RCC_CLOCKTYPE_D1PCLK1 | RCC_CLOCKTYPE_PCLK1  |
                  RCC_CLOCKTYPE_PCLK2  | RCC_CLOCKTYPE_D3PCLK1;
  clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  clk.SYSCLKDivider  = RCC_SYSCLK_DIV1;   /* 480 MHz */
  clk.AHBCLKDivider  = RCC_HCLK_DIV2;     /* 240 MHz */
  clk.APB3CLKDivider = RCC_APB3_DIV2;     /* 120 MHz */
  clk.APB1CLKDivider = RCC_APB1_DIV2;     /* 120 MHz */
  clk.APB2CLKDivider = RCC_APB2_DIV2;     /* 120 MHz */
  clk.APB4CLKDivider = RCC_APB4_DIV2;     /* 120 MHz */
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) != HAL_OK) {
    Error_Handler();
  }

  /* USB OTG FS from PLL1Q (48 MHz); I2C4 from the D3 domain APB clock. */
  perf.PeriphClockSelection = RCC_PERIPHCLK_USB | RCC_PERIPHCLK_I2C4;
  perf.UsbClockSelection    = RCC_USBCLKSOURCE_PLL;
  perf.I2c4ClockSelection   = RCC_I2C4CLKSOURCE_D3PCLK1;
  if (HAL_RCCEx_PeriphCLKConfig(&perf) != HAL_OK) {
    Error_Handler();
  }

  /* The USB transceiver needs its 3.3 V regulator and voltage detector. */
  HAL_PWREx_EnableUSBVoltageDetector();

  /* I/O compensation cell improves signal integrity at 480 MHz. */
  __HAL_RCC_CSI_ENABLE();
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  HAL_EnableCompensationCell();
}

/* ==========================================================================
 * Run LED (PG7)
 * ========================================================================== */
void bsp_led_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  LED_RUN_CLK_ENABLE();

  gpio.Pin   = LED_RUN_PIN;
  gpio.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_RUN_PORT, &gpio);

  HAL_GPIO_WritePin(LED_RUN_PORT, LED_RUN_PIN, GPIO_PIN_RESET);
}

void bsp_led_set(bool on)
{
  HAL_GPIO_WritePin(LED_RUN_PORT, LED_RUN_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void bsp_led_toggle(void)
{
  HAL_GPIO_TogglePin(LED_RUN_PORT, LED_RUN_PIN);
}

void bsp_led_task(uint32_t period_ms)
{
  static uint32_t last = 0;
  uint32_t now = HAL_GetTick();

  if ((now - last) >= period_ms) {
    last = now;
    bsp_led_toggle();
  }
}
