/**
  ******************************************************************************
  * @file    bsp_sai.c
  * @brief   SAI1_A I2S master TX + DMA2 Stream3 double buffering.
  *
  *  Ported from the ALIENTEK 音乐播放 experiment (HARDWARE/SAI/sai.c).  The
  *  clock chain and register values are kept identical because they are the
  *  part that was validated on real hardware:
  *
  *      SAI_CK = (HSE / PLLM) * PLLI2SN / PLLI2SQ / PLLI2SDIVQ
  *      (HSE = 25 MHz, PLLM = 25 -> 1 MHz VCO input)
  *
  *      MCKDIV is NOT hard-coded.  HAL_SAI_Init() derives it from
  *      Init.AudioFrequency as  MCKDIV = SAI_CK / (Fs * 512); a result of 0
  *      means "no master divider" (MCLK = SAI_CK = 256 * Fs).  That is why the
  *      44.1 kHz / 48 kHz entries below do not carry an explicit divider.
  *
  *  Double buffering:
  *      M0AR = buf0, M1AR = buf1, circular mode.  Each transfer-complete
  *      interrupt flips the controller's CT (current target) bit; the callback
  *      asks bsp_sai_free_buffer() which half just finished and the application
  *      refills exactly that half.
  *
  *  Interrupt priority 6 (>= configMAX_SYSCALL_INTERRUPT_PRIORITY) so the
  *  callback may use xSemaphoreGiveFromISR().  It must NOT touch FatFs.
  ******************************************************************************
  */
#include "bsp_sai.h"
#include "bsp_delay.h"
#include "log.h"

/* PLLI2S presets: rate, PLLI2SN, PLLI2SQ, PLLI2SDIVQ.
 * HAL_RCCEx_PeriphCLKConfig() writes (DivQ - 1) into DCKCFGR, so the values
 * here are the real dividers. */
typedef struct
{
  uint32_t rate;
  uint16_t pll_n;
  uint8_t  pll_q;
  uint8_t  pll_divq;
} SAI_PLL_PRESET_T;

static const SAI_PLL_PRESET_T s_pll_presets[] =
{
  {   8000U, 344U, 7U,  0U },
  {  11025U, 429U, 2U, 18U },
  {  16000U, 344U, 7U,  0U },
  {  22050U, 429U, 2U, 18U },
  {  32000U, 344U, 7U,  0U },
  {  44100U, 429U, 2U, 18U },
  {  48000U, 344U, 7U,  0U },
  {  88200U, 271U, 2U,  2U },
  {  96000U, 344U, 7U,  0U },
  { 176400U, 271U, 2U,  2U },
  { 192000U, 344U, 7U,  0U },
};

#define SAI_PRESET_COUNT  (sizeof(s_pll_presets) / sizeof(s_pll_presets[0]))

static SAI_HandleTypeDef s_sai;
static DMA_HandleTypeDef s_sai_dma;
static void (*s_tx_complete_cb)(void) = NULL;
static uint8_t s_sample_bytes = 2U;

/* -------------------------------------------------------------------------- */
/* HAL MSP                                                                    */
/* -------------------------------------------------------------------------- */
void HAL_SAI_MspInit(SAI_HandleTypeDef *hsai)
{
  GPIO_InitTypeDef gpio = {0};

  if (hsai->Instance != SAI1_Block_A)
  {
    return;
  }

  __HAL_RCC_SAI1_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();

  /* PE2 = MCLK_A, PE3 = SD_A, PE4 = FS_A, PE5 = SCK_A, PE6 = SD_B (unused but
   * muxed so the pin is not left floating in analog mode). */
  gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF6_SAI1;
  HAL_GPIO_Init(GPIOE, &gpio);
}

/* -------------------------------------------------------------------------- */
/* Local helpers                                                              */
/* -------------------------------------------------------------------------- */
static const SAI_PLL_PRESET_T *sai_find_preset(uint32_t rate)
{
  uint32_t i;

  for (i = 0U; i < SAI_PRESET_COUNT; i++)
  {
    if (s_pll_presets[i].rate == rate)
    {
      return &s_pll_presets[i];
    }
  }
  return NULL;
}

static void sai_apply_block_config(uint32_t data_size, uint32_t clock_strobing)
{
  HAL_SAI_DeInit(&s_sai);

  s_sai.Instance = SAI1_Block_A;
  s_sai.Init.AudioMode        = SAI_MODEMASTER_TX;
  s_sai.Init.Synchro          = SAI_ASYNCHRONOUS;
  s_sai.Init.OutputDrive      = SAI_OUTPUTDRIVE_ENABLE;
  s_sai.Init.NoDivider        = SAI_MASTERDIVIDER_ENABLE;
  s_sai.Init.FIFOThreshold    = SAI_FIFOTHRESHOLD_1QF;
  s_sai.Init.ClockSource      = SAI_CLKSOURCE_PLLI2S;
  s_sai.Init.MonoStereoMode   = SAI_STEREOMODE;
  s_sai.Init.Protocol         = SAI_FREE_PROTOCOL;
  s_sai.Init.DataSize         = data_size;
  s_sai.Init.FirstBit         = SAI_FIRSTBIT_MSB;
  s_sai.Init.ClockStrobing    = clock_strobing;

  /* 64-bit frames = 2 slots x 32 SCK, i.e. the Philips I2S framing the WM8978
   * expects (FS asserted one bit before slot 0). */
  s_sai.FrameInit.FrameLength       = 64U;
  s_sai.FrameInit.ActiveFrameLength = 32U;
  s_sai.FrameInit.FSDefinition      = SAI_FS_CHANNEL_IDENTIFICATION;
  s_sai.FrameInit.FSPolarity        = SAI_FS_ACTIVE_LOW;
  s_sai.FrameInit.FSOffset          = SAI_FS_BEFOREFIRSTBIT;

  s_sai.SlotInit.FirstBitOffset = 0U;
  s_sai.SlotInit.SlotSize       = SAI_SLOTSIZE_32B;
  s_sai.SlotInit.SlotNumber     = 2U;
  s_sai.SlotInit.SlotActive     = SAI_SLOTACTIVE_0 | SAI_SLOTACTIVE_1;

  (void)HAL_SAI_Init(&s_sai);
  __HAL_SAI_ENABLE(&s_sai);
}

/* SAI's DMA request is not enabled by HAL_SAI_Init(): CR1.DMAEN must be set
 * after the block is configured, otherwise the peripheral never asks for data.
 * The F4 HAL has no SAI_xCR1_DMAEN macro (unlike F7/H7), hence the raw bit. */
static void sai_enable_dma_request(void)
{
  SAI1_Block_A->CR1 |= (1UL << 17);
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */
void bsp_sai_set_dma_callback(void (*callback)(void))
{
  s_tx_complete_cb = callback;
}

bool bsp_sai_configure(uint8_t bits, uint32_t rate,
                       uint8_t *buf0, uint8_t *buf1, uint16_t bytes)
{
  const SAI_PLL_PRESET_T *preset;
  RCC_PeriphCLKInitTypeDef rcc = {0};
  uint32_t data_size;
  uint32_t periph_width;
  uint32_t mem_width;
  uint16_t items;

  if ((buf0 == NULL) || (buf1 == NULL) || (bytes == 0U))
  {
    return false;
  }

  preset = sai_find_preset(rate);
  if (preset == NULL)
  {
    PRINT_LOG("[SAI ] unsupported sample rate %lu\r\n", (unsigned long)rate);
    return false;
  }

  if (bits == BSP_SAI_BITS_24)
  {
    data_size    = SAI_DATASIZE_24;
    s_sample_bytes = 4U;              /* 24-bit samples left-aligned in words */
    periph_width = DMA_PDATAALIGN_WORD;
    mem_width    = DMA_MDATAALIGN_WORD;
  }
  else
  {
    data_size    = SAI_DATASIZE_16;
    s_sample_bytes = 2U;
    periph_width = DMA_PDATAALIGN_HALFWORD;
    mem_width    = DMA_MDATAALIGN_HALFWORD;
  }

  /* ---- block + PLLI2S ---- */
  sai_apply_block_config(data_size, SAI_CLOCKSTROBING_RISINGEDGE);

  rcc.PeriphClockSelection = RCC_PERIPHCLK_SAI_PLLI2S;
  rcc.PLLI2S.PLLI2SN       = preset->pll_n;
  rcc.PLLI2S.PLLI2SQ       = preset->pll_q;
  rcc.PLLI2SDivQ           = (uint32_t)preset->pll_divq + 1U;
  if (HAL_RCCEx_PeriphCLKConfig(&rcc) != HAL_OK)
  {
    PRINT_LOG("[SAI ] PLLI2S config FAILED\r\n");
    return false;
  }
  __HAL_RCC_SAI_BLOCKACLKSOURCE_CONFIG(RCC_SAIACLKSOURCE_PLLI2S);

  /* Re-init with the real AudioFrequency so HAL derives MCKDIV. */
  __HAL_SAI_DISABLE(&s_sai);
  s_sai.Init.AudioFrequency = rate;
  if (HAL_SAI_Init(&s_sai) != HAL_OK)
  {
    PRINT_LOG("[SAI ] re-init FAILED at %lu Hz\r\n", (unsigned long)rate);
    return false;
  }
  sai_enable_dma_request();
  __HAL_SAI_ENABLE(&s_sai);

  /* ---- DMA double buffer ---- */
  __HAL_RCC_DMA2_CLK_ENABLE();

  __HAL_LINKDMA(&s_sai, hdmatx, s_sai_dma);

  s_sai_dma.Instance                 = DMA2_Stream3;
  s_sai_dma.Init.Channel             = DMA_CHANNEL_0;
  s_sai_dma.Init.Direction           = DMA_MEMORY_TO_PERIPH;
  s_sai_dma.Init.PeriphInc           = DMA_PINC_DISABLE;
  s_sai_dma.Init.MemInc              = DMA_MINC_ENABLE;
  s_sai_dma.Init.PeriphDataAlignment = periph_width;
  s_sai_dma.Init.MemDataAlignment    = mem_width;
  s_sai_dma.Init.Mode                = DMA_CIRCULAR;
  s_sai_dma.Init.Priority            = DMA_PRIORITY_HIGH;
  s_sai_dma.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
  s_sai_dma.Init.FIFOThreshold       = DMA_FIFO_THRESHOLD_1QUARTERFULL;
  s_sai_dma.Init.MemBurst            = DMA_MBURST_SINGLE;
  s_sai_dma.Init.PeriphBurst         = DMA_PBURST_SINGLE;

  HAL_DMA_DeInit(&s_sai_dma);
  if (HAL_DMA_Init(&s_sai_dma) != HAL_OK)
  {
    PRINT_LOG("[SAI ] DMA init FAILED\r\n");
    return false;
  }

  items = (uint16_t)(bytes / s_sample_bytes);
  if (HAL_DMAEx_MultiBufferStart(&s_sai_dma,
                                 (uint32_t)buf0,
                                 (uint32_t)&SAI1_Block_A->DR,
                                 (uint32_t)buf1,
                                 items) != HAL_OK)
  {
    PRINT_LOG("[SAI ] DMA double-buffer start FAILED\r\n");
    return false;
  }

  /* Arm only the transfer-complete interrupt; the stream itself stays disabled
   * until bsp_sai_play() so the first refill can happen before audio starts. */
  __HAL_DMA_DISABLE(&s_sai_dma);
  bsp_delay_us(10U);   /* let the disable settle before touching CR again */
  __HAL_DMA_ENABLE_IT(&s_sai_dma, DMA_IT_TC);
  __HAL_DMA_CLEAR_FLAG(&s_sai_dma, DMA_FLAG_TCIF3_7);

  HAL_NVIC_SetPriority(DMA2_Stream3_IRQn, 6U, 0U);
  HAL_NVIC_EnableIRQ(DMA2_Stream3_IRQn);

  PRINT_LOG("[SAI ] %lu Hz / %u-bit ready (%u items per half)\r\n",
            (unsigned long)rate, (unsigned)bits, (unsigned)items);
  return true;
}

void bsp_sai_play(void)
{
  __HAL_DMA_ENABLE(&s_sai_dma);
}

void bsp_sai_stop(void)
{
  __HAL_DMA_DISABLE(&s_sai_dma);
}

uint8_t bsp_sai_free_buffer(void)
{
  /* CT = 1 -> the controller is currently feeding from M1AR (buf1), so buf0 is
   * the half that just completed and is free to refill. */
  if ((DMA2_Stream3->CR & DMA_SxCR_CT) != 0U)
  {
    return 0U;
  }
  return 1U;
}

void bsp_sai_dma_irq_handler(void)
{
  if (__HAL_DMA_GET_FLAG(&s_sai_dma, DMA_FLAG_TCIF3_7) == RESET)
  {
    return;
  }
  __HAL_DMA_CLEAR_FLAG(&s_sai_dma, DMA_FLAG_TCIF3_7);

  if (s_tx_complete_cb != NULL)
  {
    s_tx_complete_cb();
  }
}
