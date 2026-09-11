/**
 ******************************************************************************
 * @file    stm32h7xx_hal_msp.c
 * @brief   Peripheral pin / clock / DMA / NVIC setup.
 ******************************************************************************
 */

#include "main.h"
#include "bsp_camera.h"

void HAL_MspInit(void)
{
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  /* On the STM32H7 the PWR block is always clocked - there is no
   * __HAL_RCC_PWR_CLK_ENABLE() macro like on the F1/F4 families. */
}

/* ==========================================================================
 * I2C4 (SCCB) : PF14 = SCL, PF15 = SDA
 * ========================================================================== */
void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
  GPIO_InitTypeDef gpio = {0};

  if (hi2c->Instance != CAM_I2C_INSTANCE) {
    return;
  }

  __HAL_RCC_GPIOF_CLK_ENABLE();

  gpio.Pin       = CAM_I2C_SCL_PIN | CAM_I2C_SDA_PIN;
  gpio.Mode      = GPIO_MODE_AF_OD;
  gpio.Pull      = GPIO_PULLUP;
  gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = CAM_I2C_AF;
  HAL_GPIO_Init(CAM_I2C_PORT, &gpio);

  __HAL_RCC_I2C4_CLK_ENABLE();
}

void HAL_I2C_MspDeInit(I2C_HandleTypeDef *hi2c)
{
  if (hi2c->Instance != CAM_I2C_INSTANCE) {
    return;
  }
  __HAL_RCC_I2C4_CLK_DISABLE();
  HAL_GPIO_DeInit(CAM_I2C_PORT, CAM_I2C_SCL_PIN | CAM_I2C_SDA_PIN);
}

/* ==========================================================================
 * DCMI
 *   PA4  HSYNC   PA6  PIXCLK  PG9  VSYNC
 *   PC6  D0      PC7  D1      PG10 D2   PG11 D3
 *   PE4  D4      PD3  D5      PE5  D6   PE6  D7
 * DMA: DMA2_Stream1
 * ========================================================================== */
void HAL_DCMI_MspInit(DCMI_HandleTypeDef *phdcmi)
{
  GPIO_InitTypeDef gpio = {0};

  if (phdcmi->Instance != DCMI) {
    return;
  }

  __HAL_RCC_DCMI_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_DMA2_CLK_ENABLE();

  gpio.Mode      = GPIO_MODE_AF_PP;
  gpio.Pull      = GPIO_NOPULL;
  gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF13_DCMI;

  gpio.Pin = GPIO_PIN_4 | GPIO_PIN_6;                 /* HSYNC, PIXCLK */
  HAL_GPIO_Init(GPIOA, &gpio);

  gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;                 /* D0, D1 */
  HAL_GPIO_Init(GPIOC, &gpio);

  gpio.Pin = GPIO_PIN_3;                              /* D5 */
  HAL_GPIO_Init(GPIOD, &gpio);

  gpio.Pin = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6;    /* D4, D6, D7 */
  HAL_GPIO_Init(GPIOE, &gpio);

  gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11;  /* VSYNC, D2, D3 */
  HAL_GPIO_Init(GPIOG, &gpio);

  /* ---- DMA2_Stream1 : DCMI -> memory ---- */
  hdma_dcmi.Instance                 = DMA2_Stream1;
  hdma_dcmi.Init.Request             = DMA_REQUEST_DCMI;
  hdma_dcmi.Init.Direction           = DMA_PERIPH_TO_MEMORY;
  hdma_dcmi.Init.PeriphInc           = DMA_PINC_DISABLE;
  hdma_dcmi.Init.MemInc              = DMA_MINC_ENABLE;
  hdma_dcmi.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
  hdma_dcmi.Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;
  /* CIRCULAR is mandatory here: the DCMI runs in CONTINUOUS mode and never
   * stops feeding its FIFO. A NORMAL-mode stream disables itself after the
   * first frame, leaving the DCMI with nowhere to drain to -> overrun on
   * frame 2 -> HAL aborts the transfer -> capture dies at exactly one frame. */
  hdma_dcmi.Init.Mode                = DMA_CIRCULAR;
  hdma_dcmi.Init.Priority            = DMA_PRIORITY_VERY_HIGH;
  hdma_dcmi.Init.FIFOMode            = DMA_FIFOMODE_ENABLE;
  hdma_dcmi.Init.FIFOThreshold       = DMA_FIFO_THRESHOLD_FULL;
  hdma_dcmi.Init.MemBurst            = DMA_MBURST_INC4;
  hdma_dcmi.Init.PeriphBurst         = DMA_PBURST_SINGLE;

  if (HAL_DMA_Init(&hdma_dcmi) != HAL_OK) {
    Error_Handler();
  }
  __HAL_LINKDMA(phdcmi, DMA_Handle, hdma_dcmi);

  HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream1_IRQn);
  HAL_NVIC_SetPriority(DCMI_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DCMI_IRQn);
}

void HAL_DCMI_MspDeInit(DCMI_HandleTypeDef *phdcmi)
{
  if (phdcmi->Instance != DCMI) {
    return;
  }
  __HAL_RCC_DCMI_CLK_DISABLE();
  HAL_DMA_DeInit(phdcmi->DMA_Handle);
  HAL_NVIC_DisableIRQ(DCMI_IRQn);
  HAL_NVIC_DisableIRQ(DMA2_Stream1_IRQn);
}
