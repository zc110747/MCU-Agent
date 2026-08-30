/**
  ******************************************************************************
  * @file    bsp/bsp_sai_audio.c
  * @brief   SAI1_A master-TX + DMA double-buffer for WM8978 playback.
  *         See bsp_sai_audio.h for the pin / clocking notes.
  *
  *  CLOCKING (all verified against the HAL source, see below)
  *  --------------------------------------------------------
  *  From Drivers/.../stm32f4xx_hal_sai.c:
  *      line 456:  MCLK = SAI_CK / (MCKDIV * 2)   with   MCLK = 256 * FS
  *      line 465:  Mckdiv = (SAI_CK*10) / (FS*512) / 10
  *      =>  FS   = SAI_CK / (MCKDIV * 512)     (independent of frame length)
  *          BCLK = FS * FrameLength            (32 for 16-bit stereo I2S)
  *          MCLK = 256 * FS
  *
  *  Three defects were found in the previous version of this file, all of
  *  which silently corrupt audio (they produce no error and no fault):
  *
  *   1) NoDivider was set to SAI_MASTERDIVIDER_DISABLE.  That name is
  *      inverted w.r.t. the bit it sets: DISABLE == SAI_xCR1_NODIV == NODIV=1,
  *      which BYPASSES the MCLK divider.  PE2 then emitted the raw 48 MHz
  *      SAI_CK onto the WM8978 MCLK pin instead of a 256*FS master clock.
  *      Correct value is SAI_MASTERDIVIDER_ENABLE (NODIV=0 -> divider active).
  *
  *   2) FrameInit / SlotInit were never initialised.  `hsai` is static (all
  *    zeroes) and HAL_SAI_Init() writes them straight to hardware:
  *        FRCR  |= (FrameInit.FrameLength - 1)        -> 0-1 = 0xFFFFFFFF
  *        SLOTR |= ((SlotInit.SlotNumber - 1) << 8)   -> NBSLOT = 255
  *        SLOTR |= (SlotInit.SlotActive << 16)        -> SLOTEN = 0 (nothing on!)
  *      so the frame was 255 bits long with NO slot enabled.  Fixed by calling
  *      HAL_SAI_InitProtocol() (which also calls HAL_SAI_Init internally).
  *
  *   3) Init.Mckdiv = 3 was dead code: HAL_SAI_Init() overwrites it whenever
  *      AudioFrequency != SAI_AUDIO_FREQUENCY_MCKDIV.  With SAI_CK = 48 MHz
  *      the HAL-derived divider yields 46 875 Hz for BOTH 44.1 k and 48 k
  *      material (+6.29 % / -2.34 % - very audible).  48 MHz cannot express
  *      either rate exactly, so the PLLSAI is now programmed per rate family
  *      (see k_families) and Mckdiv is driven from the table by keeping
  *      AudioFrequency = SAI_AUDIO_FREQUENCY_MCKDIV.
  *
  *  AudioFrequency is used by the HAL for nothing except the Mckdiv
  *  computation (hal_sai.c lines 394/459/465), so pinning it to MCKDIV mode is
  *  safe.
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

/* Runtime clock telemetry.  These live in .bss so they can be read over SWD
 * with OpenOCD (no UART required) to confirm the clock tree on real silicon. */
volatile uint32_t g_sai_saick_hz       = 0U;
volatile uint32_t g_sai_mclk_hz        = 0U;
volatile uint32_t g_sai_bclk_hz        = 0U;
volatile uint32_t g_sai_mckdiv         = 0U;
volatile uint32_t g_sai_fs_target_hz   = 0U;
volatile uint32_t g_sai_fs_measured_hz = 0U;
/* Counts every DMA half/complete callback (SAI ISR -> HAL_DMA_IRQHandler ->
 * HAL_SAI_TxHalfCplt/TxCplt).  Lets a SWD probe distinguish "DMA streaming"
 * from "player not refilling" without a UART. */
volatile uint32_t g_sai_dma_cbs = 0U;
/* Return code of the last HAL_SAI_Transmit_DMA() call (read over SWD):
 * 0 = HAL_OK, 1 = HAL_BUSY (State != READY), 2 = HAL_ERROR, 3 = HAL_TIMEOUT,
 * 0xFFFFFFFF = never called.  Decisive for the "PLAYING but DMA silent"
 * investigation: if this is HAL_BUSY/HAL_TIMEOUT the SAI never got enabled. */
volatile uint32_t g_sai_tx_dma_ret = 0xFFFFFFFFU;

/* DMA2_Stream3 (channel 0) carries SAI1_A TX on the F429. */
#define SAI_TX_DMA_STREAM    DMA2_Stream3
#define SAI_TX_DMA_CHANNEL   DMA_CHANNEL_0
#define SAI_TX_DMA_IRQn      DMA2_Stream3_IRQn

/* ---- Clock plan ----------------------------------------------------------- */

/* VCO input for the PLLSAI: HSE / PLLM = 25 MHz / 25 = 1 MHz.  PLLM is shared
 * with the main PLL (which makes 168 MHz SYSCLK and 48 MHz USB/SDIO via PLLQ),
 * so it must NOT be touched here.  PLLSAI feeds only the SAI on this board -
 * LTDC is not enabled and the 48 MHz USB clock comes from the main PLL - so
 * N/Q/DivQ can be changed freely at run time. */
#define SAI_VCO_IN_HZ        1000000UL

/* Frame length for I2S / 16-bit / stereo (set by HAL_SAI_InitProtocol). */
#define SAI_FRAME_BITS       32UL

typedef struct
{
    uint32_t pllsai_n;
    uint32_t pllsai_q;
    uint32_t pllsai_divq;
    const char *name;
} sai_clock_family_t;

/* Three clock families.  Every entry is the result of an exhaustive search
 * (tools/audio/sai_clock_search.py) over N(50..432) x Q(2..15) x DivQ(1..32)
 * x MCKDIV(1..15) minimising |FS_actual/FS_target - 1|.
 *
 *   family      SAI_CK            covers                    worst error
 *   44.1k       22.583333 MHz     44100/22050/11025         +0.0183 %
 *   48k         24.571429 MHz     48000/24000               -0.0186 %
 *   32k         16.384615 MHz     32000/16000/8000          +0.0038 %
 *
 * 0.018 % is about 0.3 cents - several orders of magnitude below anything
 * audible, and well inside the WM8978's tolerance. */
static const sai_clock_family_t k_families[] = {
    { 271U,  2U,  6U, "44k1" },   /* 271 / (2*6)  = 22.583333 MHz */
    { 172U,  7U,  1U, "48k"  },   /* 172 / (7*1)  = 24.571429 MHz */
    { 213U, 13U,  1U, "32k"  },   /* 213 / (13*1) = 16.384615 MHz */
};
#define SAI_FAMILY_COUNT  (sizeof(k_families) / sizeof(k_families[0]))

typedef struct
{
    uint32_t pllsai_n;
    uint32_t pllsai_q;
    uint32_t pllsai_divq;
    uint32_t mckdiv;
    uint32_t saick_hz;
} sai_clock_cfg_t;

/**
 * @brief  Pick (N, Q, DivQ, MCKDIV) giving the closest achievable FS to @p sr.
 *         Cheap enough to run at track-change time (3 families x 15 dividers).
 */
static void clock_cfg_for(uint32_t sr, sai_clock_cfg_t *out)
{
    uint32_t best_err = 0xFFFFFFFFUL;
    sai_clock_cfg_t best;

    memset(&best, 0, sizeof(best));
    best.mckdiv = 1U;

    for (size_t i = 0U; i < SAI_FAMILY_COUNT; ++i)
    {
        uint32_t ck = (uint32_t)((uint64_t)k_families[i].pllsai_n *
                                 (uint64_t)SAI_VCO_IN_HZ /
                                 ((uint64_t)k_families[i].pllsai_q *
                                  (uint64_t)k_families[i].pllsai_divq));
        for (uint32_t mck = 1U; mck <= 15U; ++mck)
        {
            uint32_t fs = ck / (mck * 512UL);
            uint32_t err = (fs > sr) ? (fs - sr) : (sr - fs);

            if (err < best_err)
            {
                best_err = err;
                best.pllsai_n     = k_families[i].pllsai_n;
                best.pllsai_q     = k_families[i].pllsai_q;
                best.pllsai_divq  = k_families[i].pllsai_divq;
                best.mckdiv       = mck;
                best.saick_hz     = ck;
            }
        }
    }

    *out = best;
}

/* ---- FS self-measurement (verifiable over SWD, no scope / UART needed) ---- */

/* TIM2 is free on this board (only TIM7 is used, as the HAL time base) and is
 * one of the 32-bit timers, so it can free-run without an ISR.  DWT->CYCCNT
 * was deliberately NOT used: it is clocked by the core clock and stops while
 * the CPU sleeps in the idle task, which would over-report FS. */
static uint32_t s_tim2_hz = 0U;

#define SAI_FS_WINDOW_FRAMES  32768UL
static uint32_t s_fs_acc_frames = 0U;
static uint32_t s_fs_t0         = 0U;

static void sai_tim2_start(void)
{
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();

    /* On the F4 the timer clock is PCLK1 x2 whenever the APB1 prescaler is
     * not 1.  Board config: HCLK 168 MHz, PPRE1 = /4 -> PCLK1 = 42 MHz. */
    s_tim2_hz = ((RCC->CFGR & RCC_CFGR_PPRE1) != 0U) ? (pclk1 * 2UL) : pclk1;

    __HAL_RCC_TIM2_CLK_ENABLE();
    TIM2->CR1 = 0U;
    TIM2->PSC = 0U;
    TIM2->ARR = 0xFFFFFFFFUL;
    TIM2->CNT = 0U;
    TIM2->EGR = TIM_EGR_UG;             /* push PSC/ARR out of the shadow */
    TIM2->SR  = 0U;
    TIM2->CR1 = TIM_CR1_CEN;            /* free-run, no interrupt */
}

static void sai_fs_reset(void)
{
    s_fs_acc_frames = 0U;
    s_fs_t0         = TIM2->CNT;
}

static void sai_fs_account(uint32_t frames)
{
    s_fs_acc_frames += frames;
    if (s_fs_acc_frames >= SAI_FS_WINDOW_FRAMES)
    {
        uint32_t now = TIM2->CNT;
        uint32_t dt  = now - s_fs_t0;   /* uint32 wrap is well defined */

        if ((dt != 0U) && (s_tim2_hz != 0U))
        {
            uint64_t fs = ((uint64_t)s_fs_acc_frames * (uint64_t)s_tim2_hz) / (uint64_t)dt;
            g_sai_fs_measured_hz = (uint32_t)fs;
        }
        s_fs_acc_frames = 0U;
        s_fs_t0         = now;
    }
}

/* ---- Internal helpers ----------------------------------------------------- */
static void audio_clock_init(uint32_t sr)
{
    sai_clock_cfg_t cfg;

    clock_cfg_for(sr, &cfg);

    RCC_PeriphCLKInitTypeDef pclk = {0};
    HAL_RCCEx_GetPeriphCLKConfig(&pclk);
    pclk.PeriphClockSelection |= RCC_PERIPHCLK_SAI_PLLSAI;
    pclk.PLLSAI.PLLSAIN = cfg.pllsai_n;
    pclk.PLLSAI.PLLSAIQ = cfg.pllsai_q;
    pclk.PLLSAIDivQ     = cfg.pllsai_divq;
    if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK)
    {
        PRINT_LOG("[SAI ] PLLSAI config failed (N=%lu Q=%lu D=%lu)\r\n",
                  (unsigned long)cfg.pllsai_n, (unsigned long)cfg.pllsai_q,
                  (unsigned long)cfg.pllsai_divq);
        return;
    }

    g_sai_saick_hz = cfg.saick_hz;
    PRINT_LOG("[SAI ] PLLSAI N=%lu Q=%lu DivQ=%lu -> SAI_CK=%lu Hz\r\n",
              (unsigned long)cfg.pllsai_n, (unsigned long)cfg.pllsai_q,
              (unsigned long)cfg.pllsai_divq, (unsigned long)cfg.saick_hz);
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

/**
 * @brief  Select the SAI clocking for @p sr.
 *
 * AudioFrequency is pinned to SAI_AUDIO_FREQUENCY_MCKDIV so that
 * HAL_SAI_Init() leaves Init.Mckdiv alone - otherwise it recomputes the
 * divider from an assumed 256*FS relationship and throws the table away.
 */
static void sai_set_frequency(uint32_t sr)
{
    sai_clock_cfg_t cfg;

    clock_cfg_for(sr, &cfg);

    hsai.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_MCKDIV;
    hsai.Init.Mckdiv         = cfg.mckdiv;

    g_sai_mckdiv       = cfg.mckdiv;
    g_sai_saick_hz     = cfg.saick_hz;
    g_sai_fs_target_hz = sr;

    uint32_t fs   = cfg.saick_hz / (cfg.mckdiv * 512UL);
    uint32_t mclk = cfg.saick_hz / (cfg.mckdiv * 2UL);

    g_sai_mclk_hz = mclk;
    g_sai_bclk_hz = fs * SAI_FRAME_BITS;

    /* Error in units of 0.01 % (integer maths only - no printf float support
     * is assumed anywhere in this firmware). */
    long e100 = (long)fs - (long)sr;
    if (sr != 0U) { e100 = (e100 * 10000L) / (long)sr; }

    PRINT_LOG("[SAI ] %lu Hz -> MCKDIV=%lu FS=%lu Hz err=%+ld.%02ld%% "
              "MCLK=%lu Hz BCLK=%lu Hz\r\n",
              (unsigned long)sr, (unsigned long)cfg.mckdiv, (unsigned long)fs,
              e100 / 100L, ((e100 < 0L) ? -e100 : e100) % 100L,
              (unsigned long)mclk, (unsigned long)(fs * SAI_FRAME_BITS));
}

int sai_audio_init(uint32_t sample_rate, uint8_t channels, uint8_t bits)
{
    (void)channels; (void)bits;
    s_sr = sample_rate;

    sai_tim2_start();
    audio_clock_init(s_sr);
    audio_gpio_init();
    __HAL_RCC_SAI1_CLK_ENABLE();

    /* SAI1 block A: I2S master transmitter, 16-bit stereo.
     * Fields that HAL_SAI_InitProtocol() does NOT touch must be set here:
     * AudioMode (read to pick CKSTR), NoDivider, AudioFrequency, Mckdiv,
     * Synchro, SynchroExt, OutputDrive, FIFOThreshold, MonoStereoMode,
     * CompandingMode, TriState. */
    hsai.Instance = SAI1_Block_A;
    hsai.Init.AudioMode         = SAI_MODEMASTER_TX;
    hsai.Init.Synchro           = SAI_ASYNCHRONOUS;
    hsai.Init.SynchroExt        = SAI_SYNCEXT_OUTBLOCKA_ENABLE;
    hsai.Init.OutputDrive       = SAI_OUTPUTDRIVE_ENABLE;
    /* NODIV=0 -> the MCLK divider is ACTIVE: MCLK = SAI_CK/(MCKDIV*2).
     * (SAI_MASTERDIVIDER_DISABLE would set NODIV=1 and bypass it.) */
    hsai.Init.NoDivider         = SAI_MASTERDIVIDER_ENABLE;
    hsai.Init.FIFOThreshold     = SAI_FIFOTHRESHOLD_1QF;
    hsai.Init.MonoStereoMode    = SAI_STEREOMODE;
    hsai.Init.CompandingMode    = SAI_NOCOMPANDING;
    hsai.Init.TriState          = SAI_OUTPUT_NOTRELEASED;
    sai_set_frequency(s_sr);

    /* DMA link (configure before the SAI init so the init sees it). */
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

    /* DMA stream IRQ priority MUST be STRICTLY BELOW
     * configMAX_SYSCALL_INTERRUPT_PRIORITY (here =5), i.e. numerically LOWER
     * (4).  Rationale: FreeRTOS's portDISABLE_INTERRUPTS() writes
     * BASEPRI = configMAX_SYSCALL_INTERRUPT_PRIORITY (=0x50) during every
     * critical section, which masks ANY interrupt with priority >= that value
     * (the comparison is strict: priority < BASEPRI is serviced, priority >=
     * BASEPRI is deferred).  So an ISR set to EXACTLY 5 would be MASKED during
     * critical sections and would never fire -> the refill semaphore is never
     * given -> SAI starves -> no audio.  Setting it to 4 keeps it unmasked yet
     * still <= configMAX_SYSCALL_INTERRUPT_PRIORITY so it may legally call
     * xSemaphoreGiveFromISR().  The TxHalfCplt/TxCplt callbacks do exactly that. */
    HAL_NVIC_SetPriority(SAI_TX_DMA_IRQn,
                         (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY - 1U), 0);
    HAL_NVIC_EnableIRQ(SAI_TX_DMA_IRQn);

    /* HAL_SAI_InitProtocol() fills FrameInit/SlotInit for I2S + 16-bit + 2
     * slots (FRL=32, FSALL=16, SLOTEN=all) and then calls HAL_SAI_Init().
     * Skipping it left those structs at zero -> 255-bit frames with no
     * enabled slot. */
    if (HAL_SAI_InitProtocol(&hsai, SAI_I2S_STANDARD,
                             SAI_PROTOCOL_DATASIZE_16BIT, 2U) != HAL_OK)
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

    sai_fs_reset();
    PRINT_LOG("[SAI ] init OK (sr=%lu, buf=%lu KB)\r\n",
              (unsigned long)s_sr, (unsigned long)(bytes / 1024U));
    return 0;
}

int sai_audio_configure(uint32_t sample_rate)
{
    if (sample_rate == s_sr) return 0;
    sai_audio_stop();
    s_sr = sample_rate;

    /* The PLLSAI is part of the clock plan, so it has to be reprogrammed for
     * the new rate too - not just the SAI divider. */
    audio_clock_init(s_sr);
    sai_set_frequency(s_sr);

    if (HAL_SAI_InitProtocol(&hsai, SAI_I2S_STANDARD,
                             SAI_PROTOCOL_DATASIZE_16BIT, 2U) != HAL_OK)
    {
        PRINT_LOG("[SAI ] re-config failed\r\n");
        return -1;
    }
    __HAL_LINKDMA(&hsai, hdmatx, hdma_sai);
    sai_fs_reset();
    return 0;
}

void sai_audio_start(void)
{
    if (s_buf == NULL) return;
    sai_fs_reset();
    /* Total 16-bit samples = 2 halves * s_half frames * 2 channels. */
    uint32_t total = 2U * s_half * 2U;
    HAL_StatusTypeDef st = HAL_SAI_Transmit_DMA(&hsai, (uint8_t *)s_buf, total);
    g_sai_tx_dma_ret = (uint32_t)st;
    if (st != HAL_OK)
    {
        PRINT_LOG("[SAI ] Transmit_DMA failed (ret=%lu)\r\n",
                  (unsigned long)st);
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
        g_sai_dma_cbs++;
        sai_fs_account(s_half);
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
        g_sai_dma_cbs++;
        sai_fs_account(s_half);
        s_empty_half = 1U;
        if (s_sai_sem != NULL)
        {
            BaseType_t hp = pdFALSE;
            xSemaphoreGiveFromISR(s_sai_sem, &hp);
            portYIELD_FROM_ISR(hp);
        }
    }
}

/* DMA2_Stream3 (channel 0) carries SAI1_A TX on the F429.  Without a STRONG
 * definition here the startup vector falls through to Default_Handler, so the
 * instant playback starts (HAL_SAI_Transmit_DMA enables the stream) the DMA
 * HalfTransfer/TransferComplete interrupts fire into an infinite "b ." loop and
 * the whole board freezes on "play" -- CFSR stays 0 because it is an unhandled
 * IRQ, not a fault, and higher-priority ISRs (e.g. touch) keep preempting it.
 * HAL_DMA_IRQHandler() dispatches to the SAI half/cplt callbacks above, which
 * drive the refill semaphore.  This is the click-to-freeze root cause. */
void DMA2_Stream3_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_sai);
}
