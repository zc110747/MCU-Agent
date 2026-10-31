/**
 ******************************************************************************
 * @file    bsp_sdio.c
 * @brief   microSD card driver over SDIO (4-bit wide bus), DMA receive.
 *
 *  Based on embedded_based_on_stm32/code/00-Drivers/drv_sdio.c.
 *
 *  Retry policy
 *  ------------
 *  bsp_sdio_init() may be called as often as the caller likes.  A failure can
 *  mean "no card in the socket" as much as "real error", so the state is not
 *  latched: every call re-arms the peripheral and re-runs the full
 *  enumeration.  With no card installed SD_PowerON() bails out on the first
 *  CMD55 (no response -> error returned), so a failed attempt costs well
 *  under a millisecond and is safe to repeat from a task loop.
 *
 *  Data path
 *  ---------
 *  Reads are moved by DMA2 Stream 3 Channel 4 straight out of the 32-bit SDIO
 *  FIFO.  The FIFO holds 32 words, so a polled drain loop has to service it
 *  every ~1.5 us at 21 MHz; one interrupt landing inside that loop is enough
 *  to overrun it (RX_OVERRUN, FR_DISK_ERR).  With DMA the transfer is
 *  independent of CPU timing.
 ******************************************************************************
 */
#include "bsp_sdio.h"

#include <stdio.h>
#include <string.h>

#include "log.h"

SD_HandleTypeDef  hsd_card;
DMA_HandleTypeDef hdma_sdio_rx;

static uint8_t s_ready = 0U;
static uint8_t s_err_logged = 0U;
static uint32_t s_err_code = 0U;
static uint32_t s_err_sta = 0U;
static uint32_t s_err_dctrl = 0U;
static uint32_t s_err_ndtr = 0U;
static uint32_t s_err_lisr = 0U;
static uint32_t s_err_state = 0U;

/* Completion handshake between the ISR and bsp_sdio_read_blocks().  Both are
 * written from the DMA / SDIO interrupt, which is why they are volatile. */
static volatile uint8_t s_rx_done = 0U;
static volatile uint8_t s_rx_err  = 0U;

/* MBURST_INC4 requires a 16-byte aligned destination.  FatFs goes through
 * disk_read(), which already bounces odd buffers, but a direct caller could
 * still hand us a word-aligned-only pointer -- one block at a time through
 * here keeps that from becoming a silent DMA error. */
static uint8_t s_rx_bounce[SDIO_BLOCK_SIZE] __attribute__((aligned(32)));

/* -------------------------------------------------------------------------- */
/* HAL callbacks (weak in stm32f4xx_hal_sd.c)                                  */
/* -------------------------------------------------------------------------- */

void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)
{
    if (hsd == &hsd_card)
    {
        s_rx_done = 1U;
    }
}

void HAL_SD_ErrorCallback(SD_HandleTypeDef *hsd)
{
    if (hsd == &hsd_card)
    {
        s_rx_err  = 1U;
        s_rx_done = 1U;
    }
}

void HAL_SD_AbortCallback(SD_HandleTypeDef *hsd)
{
    if (hsd == &hsd_card)
    {
        s_rx_done = 1U;
    }
}

/* -------------------------------------------------------------------------- */
/* Hardware bring-up                                                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief  DMA2 Stream 3 Channel 4 for SDIO_RX.
 *
 *  Word-wide on both sides: the SDIO FIFO is a 32-bit register and the HAL
 *  starts the transfer with a length in words.
 *
 *  Mode = DMA_PFCTRL is mandatory, not a tuning choice.  The SDIO asserts its
 *  DMA request only while the RX FIFO is at least half full (RXFIFOHF), so in
 *  normal (memory-controlled) mode the last few words of a block never raise a
 *  request and the transfer stalls with a handful of beats outstanding --
 *  measured as ndtr=4 while the SDIO had already seen DATAEND.  With the
 *  peripheral as flow controller the SDIO itself terminates the transfer, and
 *  FIFO/burst settings match ST's own SDIO example.
 */
static void sdio_dma_init(void)
{
    __HAL_RCC_DMA2_CLK_ENABLE();

    /* DeInit() dereferences Instance, so it has to be set first. */
    hdma_sdio_rx.Instance = DMA2_Stream3;
    (void)HAL_DMA_DeInit(&hdma_sdio_rx);

    hdma_sdio_rx.Init.Channel             = DMA_CHANNEL_4;
    hdma_sdio_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_sdio_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_sdio_rx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_sdio_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_sdio_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;
    hdma_sdio_rx.Init.Mode                = DMA_PFCTRL;
    hdma_sdio_rx.Init.Priority            = DMA_PRIORITY_VERY_HIGH;
    hdma_sdio_rx.Init.FIFOMode            = DMA_FIFOMODE_ENABLE;
    hdma_sdio_rx.Init.FIFOThreshold       = DMA_FIFO_THRESHOLD_FULL;
    hdma_sdio_rx.Init.MemBurst            = DMA_MBURST_INC4;
    hdma_sdio_rx.Init.PeriphBurst         = DMA_PBURST_INC4;

    if (HAL_DMA_Init(&hdma_sdio_rx) != HAL_OK)
    {
        PRINT_LOG("[SDIO] DMA init failed\r\n");
        return;
    }

    /* Wires hsd_card.hdmarx + hdma_sdio_rx.Parent, which HAL_SD_ReadBlocks_DMA()
     * needs before it touches hdmarx->XferCpltCallback. */
    __HAL_LINKDMA(&hsd_card, hdmarx, hdma_sdio_rx);

    HAL_NVIC_SetPriority(DMA2_Stream3_IRQn, SDIO_DMA_IRQ_PRIO, 0U);
    HAL_NVIC_EnableIRQ(DMA2_Stream3_IRQn);

    HAL_NVIC_SetPriority(SDIO_IRQn, SDIO_IRQ_PRIO, 0U);
    HAL_NVIC_EnableIRQ(SDIO_IRQn);
}

/**
 * @brief  SDIO + GPIO + DMA clocks, pin mux and vectors (before every
 *         HAL_SD_Init).
 */
static void sdio_msp_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* Reset the peripheral so a retry starts from a known state. */
    __HAL_RCC_SDIO_FORCE_RESET();
    __HAL_RCC_SDIO_RELEASE_RESET();

    __HAL_RCC_SDIO_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* PC8..PC12 -> SDIO_D0..D3 + SDIO_CK */
    gpio.Pin       = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 |
                     GPIO_PIN_12;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF12_SDIO;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* PD2 -> SDIO_CMD */
    gpio.Pin = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOD, &gpio);

    sdio_dma_init();
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

GlobalType_t bsp_sdio_init(void)
{
    sdio_msp_init();

    hsd_card.Instance                 = SDIO;
    hsd_card.Init.ClockEdge           = SDIO_CLOCK_EDGE_RISING;
    hsd_card.Init.ClockBypass         = SDIO_CLOCK_BYPASS_DISABLE;
    hsd_card.Init.ClockPowerSave      = SDIO_CLOCK_POWER_SAVE_DISABLE;
    hsd_card.Init.BusWide             = SDIO_BUS_WIDE_1B;
    hsd_card.Init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
    hsd_card.Init.ClockDiv            = SDIO_CLOCK_DIV;

    if (HAL_SD_Init(&hsd_card) != HAL_OK)
    {
        s_ready = 0;
        return RT_FAIL;
    }

    /* Enumerated in 1-bit; widen to 4-bit for the data phase. */
    if (HAL_SD_ConfigWideBusOperation(&hsd_card, SDIO_BUS_WIDE_4B) != HAL_OK)
    {
        HAL_SD_DeInit(&hsd_card);
        s_ready = 0;
        return RT_FAIL;
    }

    s_ready = 1;

    /* Drop any flag left over from enumeration / a previous aborted attempt.
     * HAL_SD_ReadBlocks_DMA() does not clear the static flags before it starts:
     * a stale RXOVERR makes the very first block read bail out immediately and
     * report RX_OVERRUN while the data is in fact still streaming in. */
    __HAL_SD_CLEAR_FLAG(&hsd_card, SDIO_STATIC_FLAGS);

    /* Clock baseline: the SDIO kernel clock is PCLK2 / (ClockDiv + 2).  If the
     * core clock the HAL computed is wrong, 400 kHz enumeration still works but
     * the data phase runs far too fast and every block transfer fails -- which
     * looks exactly like a broken FAT. */
    PRINT_LOG("[SDIO] core=%lu pclk2=%lu sdio=%lu div=%u dma=on\r\n",
              (unsigned long)SystemCoreClock,
              (unsigned long)HAL_RCC_GetPCLK2Freq(),
              (unsigned long)(HAL_RCC_GetPCLK2Freq() / (SDIO_CLOCK_DIV + 2U)),
              (unsigned int)SDIO_CLOCK_DIV);
    return RT_OK;
}

void bsp_sdio_deinit(void)
{
    HAL_SD_DeInit(&hsd_card);
    s_ready = 0;
}

uint8_t bsp_sdio_is_ready(void)
{
    return s_ready;
}

HAL_StatusTypeDef bsp_sdio_read_blocks(uint8_t *buf, uint32_t start_block, uint32_t nblocks)
{
    HAL_StatusTypeDef st = HAL_ERROR;
    uint32_t          waited;
    uint8_t           attempt;

    if (!s_ready || (buf == NULL) || (nblocks == 0U))
    {
        return HAL_ERROR;
    }

    /* MBURST_INC4 needs 16-byte alignment; bounce one sector at a time when the
     * caller cannot provide it. */
    if ((((uint32_t)buf) & 15U) != 0U)
    {
        uint32_t i;

        for (i = 0U; i < nblocks; i++)
        {
            HAL_StatusTypeDef one = bsp_sdio_read_blocks(s_rx_bounce,
                                                         start_block + i, 1U);
            if (one != HAL_OK)
            {
                return one;
            }
            (void)memcpy(buf + (i * SDIO_BLOCK_SIZE), s_rx_bounce, SDIO_BLOCK_SIZE);
        }
        return HAL_OK;
    }

    /* A failed data transfer leaves the SDIO data path state machine stuck:
     * every following command then fails as well, which is exactly what "all
     * 354 glyph reads return FR_DISK_ERR" looks like.  Abort the transfer and
     * retry twice before giving the error back to the caller.
     *
     * The diagnostics must be sampled BEFORE the first abort: HAL_SD_Abort()
     * resets ErrorCode and clears every status flag, so reading them afterwards
     * reports a clean controller and hides the actual failure. */
    for (attempt = 0U; attempt <= SDIO_RW_RETRIES; attempt++)
    {
        /* Every attempt starts from a clean slate -- see the note in
         * bsp_sdio_init(). */
        __HAL_SD_CLEAR_FLAG(&hsd_card, SDIO_STATIC_FLAGS);
        NVIC_ClearPendingIRQ(SDIO_IRQn);
        NVIC_ClearPendingIRQ(DMA2_Stream3_IRQn);

        s_rx_done = 0U;
        s_rx_err  = 0U;

        st = HAL_SD_ReadBlocks_DMA(&hsd_card, buf, start_block, nblocks);
        if (st != HAL_OK)
        {
            if (attempt == 0U)
            {
                s_err_code  = hsd_card.ErrorCode;
                s_err_sta   = SDIO->STA;
                s_err_dctrl = SDIO->DCTRL;
            }
            (void)HAL_SD_Abort(&hsd_card);
            HAL_Delay(1U);
            continue;
        }

        /* The DMA + SDIO interrupts finish the transfer and set s_rx_done from
         * HAL_SD_RxCpltCallback() / HAL_SD_ErrorCallback().  Nothing here has
         * to touch the FIFO, so a task switch or a long ISR cannot corrupt it.
         * HAL_GetTick() runs off TIM7, which keeps counting no matter what the
         * FreeRTOS scheduler is doing. */
        waited = HAL_GetTick();
        while ((s_rx_done == 0U) && ((HAL_GetTick() - waited) < SDIO_RW_TIMEOUT_MS))
        {
            /* spin: ~200 us for one 512-byte block at 21 MHz */
        }

        if ((s_rx_done != 0U) && (s_rx_err == 0U) &&
            (hsd_card.ErrorCode == HAL_SD_ERROR_NONE))
        {
            st = HAL_OK;
            break;
        }

        st = (s_rx_done == 0U) ? HAL_TIMEOUT : HAL_ERROR;
        if (attempt == 0U)
        {
            s_err_code  = hsd_card.ErrorCode;
            s_err_sta   = SDIO->STA;
            s_err_dctrl = SDIO->DCTRL;
            s_err_ndtr  = DMA2_Stream3->NDTR;
            s_err_lisr  = DMA2->LISR;
            s_err_state = (uint32_t)hsd_card.State;
        }

        (void)HAL_SD_Abort(&hsd_card);
        HAL_Delay(1U);
    }

    if (st != HAL_OK)
    {
        /* One line per failure burst is enough to identify the SDIO error class
         * (timeout / CRC / FIFO overrun) without flooding the UART. */
        if (s_err_logged == 0U)
        {
            s_err_logged = 1U;
            PRINT_LOG("[SDIO] read fail st=%d err=0x%08lX STA=0x%08lX "
                      "DCTRL=0x%08lX ndtr=%lu lisr=0x%08lX hst=%u blk=%lu\r\n",
                      (int)st, (unsigned long)s_err_code, (unsigned long)s_err_sta,
                      (unsigned long)s_err_dctrl, (unsigned long)s_err_ndtr,
                      (unsigned long)s_err_lisr, (unsigned int)s_err_state,
                      (unsigned long)start_block);
        }
        return st;
    }
    s_err_logged = 0U;

    /* The transfer is complete once the DMA callback runs, but the card itself
     * may still be busy (write-back / internal state machine).  Wait until it
     * reports TRANSFER so the next command is not issued too early. */
    waited = 0U;
    while (HAL_SD_GetCardState(&hsd_card) != HAL_SD_CARD_TRANSFER)
    {
        if (waited++ >= SDIO_RW_TIMEOUT_MS)
        {
            (void)HAL_SD_Abort(&hsd_card);
            return HAL_TIMEOUT;
        }
        HAL_Delay(1);
    }

    return HAL_OK;
}

HAL_StatusTypeDef bsp_sdio_write_blocks(const uint8_t *buf, uint32_t start_block, uint32_t nblocks)
{
    HAL_StatusTypeDef st = HAL_ERROR;
    uint32_t          waited;
    uint8_t           attempt;

    if (!s_ready)
    {
        return HAL_ERROR;
    }

    /* Same retry policy as the read path: abort the stuck transfer first, then
     * try again (see bsp_sdio_read_blocks).  Writes stay polled -- the card is
     * only written by maintenance tools, never by the runtime UI. */
    for (attempt = 0U; attempt <= SDIO_RW_RETRIES; attempt++)
    {
        st = HAL_SD_WriteBlocks(&hsd_card, (uint8_t *)buf, start_block, nblocks,
                                SDIO_RW_TIMEOUT_MS);
        if (st == HAL_OK)
        {
            break;
        }

        (void)HAL_SD_Abort(&hsd_card);
        HAL_Delay(1U);
    }

    if (st != HAL_OK)
    {
        return st;
    }

    waited = 0U;
    while (HAL_SD_GetCardState(&hsd_card) != HAL_SD_CARD_TRANSFER)
    {
        if (waited++ >= SDIO_RW_TIMEOUT_MS)
        {
            (void)HAL_SD_Abort(&hsd_card);
            return HAL_TIMEOUT;
        }
        HAL_Delay(1);
    }

    return HAL_OK;
}

GlobalType_t bsp_sdio_get_info(uint32_t *block_count, uint16_t *block_size)
{
    HAL_SD_CardInfoTypeDef info;

    if (!s_ready)
    {
        return RT_FAIL;
    }

    if (HAL_SD_GetCardInfo(&hsd_card, &info) != HAL_OK)
    {
        return RT_FAIL;
    }

    if (block_count != NULL)
    {
        *block_count = info.BlockNbr;
    }
    if (block_size != NULL)
    {
        *block_size = (uint16_t)info.BlockSize;
    }

    return RT_OK;
}
