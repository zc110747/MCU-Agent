/**
  ******************************************************************************
  * @file    bsp/bsp_sai_audio.c
  * @brief   SAI1_A master-TX + DMA double-buffer for WM8978 playback.
  *         See bsp_sai_audio.h for the pin / clocking notes.
  ******************************************************************************
  */
#include "bsp_sai_audio.h"
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "log.h"

#include <string.h>

/* ---- Local state ---------------------------------------------------------- */
static SAI_HandleTypeDef   hsai;
static DMA_HandleTypeDef   hdma_sai;
static uint16_t           *s_buf = NULL;       /* SDRAM double-buffer       */
static uint32_t            s_half = AUDIO_HALF_FRAMES; /* frames per half    */
static uint8_t             s_empty_half = 0U;  /* which half to fill next   */
static SemaphoreHandle_t   s_sai_sem = NULL;  /* given by DMA ISR          */
static uint32_t            s_sr = 44100U;

/* DMA2_Stream3 (channel 0) carries SAI1_A TX on the F429. */
#define SAI_TX_DMA_STREAM    DMA2_Stream3
#define SAI_TX_DMA_CHANNEL   DMA_CHANNEL_0
#define SAI_TX_DMA_IRQn      DMA2_Stream3_IRQn

/* ---- Internal helpers ----------------------------------------------------- */
static void audio_clock_init(void)
{
    /* SAI1CLK from PLLSAI.  On the F429 the SAI clock is selected by the
     * RCC_PERIPHCLK_SAI_PLLSAI bit (there is no Sai1ClockSelection member in
     * this HAL variant) and sourced from PLLSAI_Q / PLLSAIDivQ.  With
     * HSE=25 MHz and PLLM=25 the VCO input is 1 MHz; PLLSAIN=192 -> 192 MHz
     * VCO; PLLSAIQ=4 -> 48 MHz; PLLSAIDivQ=1 -> 48 MHz SAI1CLK.  The MCK/BCLK
     * dividers are a hardware-verification item (see README) -- tune Mckdiv /
     * PLLSAIN on the bench with a scope. */
    RCC_PeriphCLKInitTypeDef pclk = {0};
    HAL_RCCEx_GetPeriphCLKConfig(&pclk);
    pclk.PeriphClockSelection |= RCC_PERIPHCLK_SAI_PLLSAI;
    pclk.PLLSAI.PLLSAIN = 192;
    pclk.PLLSAI.PLLSAIQ = 4;
    pclk.PLLSAIDivQ = 1;
    if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK)
    {
        PRINT_LOG("[SAI ] PLLSAI/SAI clock config failed\r\n");
    }
}

static void audio_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOE_CLK_ENABLE();

    /* PE2..PE6 -> SAI1_A, AF6. */
    gpio.Pin   = GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 |
                 GPIO_PIN_5 | GPIO_PIN_6;
    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF6_SAI1;
    HAL_GPIO_Init(GPIOE, &gpio);
}

static void sai_set_frequency(uint32_t sr)
{
    switch (sr)
    {
        case 48000U: hsai.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_48K; break;
        case 44100U: hsai.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_44K; break;
        case 32000U: hsai.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_32K; break;
        case 22050U: hsai.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_22K; break;
        case 16000U: hsai.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_16K; break;
        case 11025U: hsai.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_11K; break;
        case 8000U:  hsai.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_8K;  break;
        default:     hsai.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_44K; break;
    }
    /* MCLK out divider: ~12 MHz from 48 MHz SAI1CLK (256xFs-ish for 44.1/48k).
     * Tune on HW if the CODEC is silent or off-rate. */
    hsai.Init.Mckdiv = 3U;
}

int sai_audio_init(uint32_t sample_rate, uint8_t channels, uint8_t bits)
{
    (void)channels; (void)bits;
    s_sr = sample_rate;

    audio_clock_init();
    audio_gpio_init();
    __HAL_RCC_SAI1_CLK_ENABLE();

    /* SAI1 block A: I2S master transmitter, 16-bit stereo. */
    hsai.Instance = SAI1_Block_A;
    hsai.Init.Protocol          = SAI_I2S_STANDARD;
    hsai.Init.AudioMode         = SAI_MODEMASTER_TX;
    hsai.Init.DataSize          = SAI_DATASIZE_16;
    hsai.Init.FirstBit          = SAI_FIRSTBIT_MSB;
    hsai.Init.ClockStrobing     = SAI_CLOCKSTROBING_FALLINGEDGE;
    hsai.Init.Synchro           = SAI_ASYNCHRONOUS;
    hsai.Init.OutputDrive       = SAI_OUTPUTDRIVE_ENABLE;
    hsai.Init.NoDivider         = SAI_MASTERDIVIDER_DISABLE; /* divide MCK */
    hsai.Init.FIFOThreshold     = SAI_FIFOTHRESHOLD_1QF;
    hsai.Init.SynchroExt        = SAI_SYNCEXT_OUTBLOCKA_ENABLE;
    hsai.Init.MonoStereoMode    = SAI_STEREOMODE;
    hsai.Init.CompandingMode    = SAI_NOCOMPANDING;
    hsai.Init.TriState          = SAI_OUTPUT_NOTRELEASED;
    /* MCLK (PE2) is output automatically for a master block in this HAL; the
     * divider below sets its frequency (~12 MHz from 48 MHz SAI1CLK).  The
     * exact MCK/BCLK ratio is a hardware-verification item -- see README. */
    sai_set_frequency(s_sr);

    /* DMA link (configure before HAL_SAI_Init so the init sees it). */
    __HAL_RCC_DMA2_CLK_ENABLE();
    hdma_sai.Instance = SAI_TX_DMA_STREAM;
    hdma_sai.Init.Channel             = SAI_TX_DMA_CHANNEL;
    hdma_sai.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    hdma_sai.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_sai.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_sai.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_sai.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    hdma_sai.Init.Mode                = DMA_CIRCULAR;
    hdma_sai.Init.Priority            = DMA_PRIORITY_HIGH;
    hdma_sai.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    hdma_sai.Init.FIFOThreshold       = DMA_FIFO_THRESHOLD_FULL;
    hdma_sai.Init.MemBurst            = DMA_MBURST_SINGLE;
    hdma_sai.Init.PeriphBurst         = DMA_PBURST_SINGLE;
    if (HAL_DMA_Init(&hdma_sai) != HAL_OK)
    {
        PRINT_LOG("[SAI ] DMA init failed\r\n");
        return -1;
    }
    __HAL_LINKDMA(&hsai, hdmatx, hdma_sai);

    /* DMA stream IRQ priority: 6 (>= configMAX_SYSCALL_INTERRUPT_PRIORITY 5)
     * so the FromISR semaphore give inside the callbacks is legal. */
    HAL_NVIC_SetPriority(SAI_TX_DMA_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(SAI_TX_DMA_IRQn);

    if (HAL_SAI_Init(&hsai) != HAL_OK)
    {
        PRINT_LOG("[SAI ] HAL_SAI_Init failed\r\n");
        return -1;
    }

    /* Double buffer in SDRAM (matches the "audio in SDRAM" requirement). */
    uint32_t bytes = (uint32_t)(2U * s_half * 2U * sizeof(uint16_t));
    s_buf = (uint16_t *)pvPortMalloc(bytes);
    if (s_buf == NULL)
    {
        PRINT_LOG("[SAI ] SDRAM buffer alloc FAILED\r\n");
        return -1;
    }
    (void)memset(s_buf, 0, bytes);

    s_sai_sem = xSemaphoreCreateBinary();
    if (s_sai_sem == NULL)
    {
        PRINT_LOG("[SAI ] sem create FAILED\r\n");
        return -1;
    }

    PRINT_LOG("[SAI ] init OK (sr=%lu, buf=%lu KB)\r\n",
              (unsigned long)s_sr, (unsigned long)(bytes / 1024U));
    return 0;
}

int sai_audio_configure(uint32_t sample_rate)
{
    if (sample_rate == s_sr) return 0;
    sai_audio_stop();
    s_sr = sample_rate;
    sai_set_frequency(s_sr);
    if (HAL_SAI_Init(&hsai) != HAL_OK)
    {
        PRINT_LOG("[SAI ] re-config failed\r\n");
        return -1;
    }
    __HAL_LINKDMA(&hsai, hdmatx, hdma_sai);
    return 0;
}

void sai_audio_start(void)
{
    if (s_buf == NULL) return;
    /* Total 16-bit samples = 2 halves * s_half frames * 2 channels. */
    uint32_t total = 2U * s_half * 2U;
    if (HAL_SAI_Transmit_DMA(&hsai, (uint8_t *)s_buf, total) != HAL_OK)
    {
        PRINT_LOG("[SAI ] Transmit_DMA failed\r\n");
    }
}

void sai_audio_stop(void)
{
    if (hsai.Instance != NULL)
    {
        HAL_SAI_DMAStop(&hsai);
    }
}

uint16_t *sai_audio_buffer(void)
{
    return s_buf;
}

uint32_t sai_audio_half_frames(void)
{
    return s_half;
}

uint8_t sai_audio_get_empty_half(void)
{
    if (s_sai_sem != NULL)
    {
        (void)xSemaphoreTake(s_sai_sem, portMAX_DELAY);
    }
    return s_empty_half;
}

void sai_audio_drain(void)
{
    if (s_sai_sem != NULL)
    {
        /* Binary semaphore: at most one token can be pending; a single
         * non-blocking take clears it. */
        (void)xSemaphoreTake(s_sai_sem, 0);
    }
}

/* ---- DMA half/complete callbacks (refill signalling) ---------------------- */
void HAL_SAI_TxHalfCpltCallback(SAI_HandleTypeDef *h)
{
    if ((h != NULL) && (h->Instance == SAI1_Block_A))
    {
        s_empty_half = 0U;
        if (s_sai_sem != NULL)
        {
            BaseType_t hp = pdFALSE;
            xSemaphoreGiveFromISR(s_sai_sem, &hp);
            portYIELD_FROM_ISR(hp);
        }
    }
}

void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *h)
{
    if ((h != NULL) && (h->Instance == SAI1_Block_A))
    {
        s_empty_half = 1U;
        if (s_sai_sem != NULL)
        {
            BaseType_t hp = pdFALSE;
            xSemaphoreGiveFromISR(s_sai_sem, &hp);
            portYIELD_FROM_ISR(hp);
        }
    }
}
