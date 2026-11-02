/**
 ******************************************************************************
 * @file    bsp_sdio.c
 * @brief   microSD card driver over SDIO (4-bit wide bus): DMA reads, polled writes.
 *
 *  Based on embedded_based_on_stm32/code/00-Drivers/drv_sdio.c, then reworked
 *  for this project.
 *
 *  Read path
 *  ---------
 *  HAL_SD_ReadBlocks_DMA() + DMA2 Stream6 / Channel 4 (the SDIO request line
 *  on the F429; the audio stream keeps DMA2 Stream3, so neither path has to be
 *  reconfigured when the other one runs).  One binary semaphore carries the
 *  completion, given from the DMA transfer-complete ISR via HAL_SD_RxCpltCallback().
 *
 *  Two failure modes are handled here rather than in the caller:
 *
 *   1. Stranded FIFO tail.  When the card's data path ends, SDIO drops the DMA
 *      request; the last words stay queued in the 32-word FIFO and the stream
 *      never reaches TC (measured: NDTR frozen at 4, i.e. 16 bytes per block).
 *      bsp_sdio_irq_handler() pulls them out by hand -- BEFORE the HAL clears
 *      DCTRL.DTEN, which would flush them -- and the burst is closed with
 *      HAL_SD_Abort() (that also emits the CMD12 a multi-block read needs).
 *      SDIO->FIFOCNT reads 0 while those words are really there, so NDTR is the
 *      only usable count.
 *
 *   2. Everything else.  HAL_SD_Abort() + retry, SDIO_IO_RETRIES times.  Reads
 *      are idempotent so replaying the same chunk is always safe.
 *
 *  Writes stay polled: they are rare (USB MSC write10) and the TX FIFO is
 *  drained by the CPU without the overrun risk the RX direction has.
 *
 *  Retry policy of bsp_sdio_init()
 *  -------------------------------
 *  bsp_sdio_init() may be called as often as the caller likes.  A failure can
 *  mean "no card in the socket" as much as "real error", so the state is not
 *  latched: every call re-arms the peripheral and re-runs the full enumeration.
 ******************************************************************************
 */
#include "bsp_sdio.h"

#include <stdbool.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "log.h"

SD_HandleTypeDef hsd_card;

static uint8_t s_ready = 0U;

/* -------------------------------------------------------------------------- */
/* DMA receive channel                                                        */
/* -------------------------------------------------------------------------- */

#define SDIO_DMA_STREAM   DMA2_Stream6
#define SDIO_DMA_CHANNEL  DMA_CHANNEL_4
#define SDIO_DMA_IRQn     DMA2_Stream6_IRQn

/* Both of these sit at priority 6, i.e. below
 * configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (5), so the completion
 * callbacks are allowed to use the FromISR API. */
#define SDIO_IRQ_PRIO 6U

static DMA_HandleTypeDef s_hdma_rx;
static SemaphoreHandle_t s_dma_sem = NULL;

/* Written by the ISRs, cleared by the task before every burst. */
#define SDIO_DMA_IDLE 0U
#define SDIO_DMA_OK   1U
#define SDIO_DMA_ERR  2U
static volatile uint8_t s_dma_done = SDIO_DMA_IDLE;

/* DATAEND bookkeeping: lets the task tell "the card has finished, the stream is
 * short" apart from "the transfer is still running". */
static volatile uint8_t  s_dataend    = 0U;
static volatile uint32_t s_dataend_ms = 0U;

/* Burst in flight, so the ISR knows where a stranded FIFO tail belongs. */
static volatile uint8_t *s_cur_buf   = NULL;
static volatile uint32_t s_cur_bytes = 0U;

/* Set when the ISR recovered the tail itself: the task must still close the
 * transfer (the HAL left hsd_card.State at BUSY). */
static volatile uint8_t s_need_abort = 0U;

/* -------------------------------------------------------------------------- */
/* Tunables                                                                   */
/* -------------------------------------------------------------------------- */

/* 8 KB per burst.  With DMA draining the FIFO there is no CPU-latency window
 * to protect any more, so the bursts can be long; the cap only bounds the
 * worst-case latency of one retry. */
#define SDIO_CHUNK_BLOCKS 16U

/* Retries *after* the first attempt, i.e. three attempts in total. */
#define SDIO_IO_RETRIES 2U

/* How long to wait for the stream to finish after the card said DATAEND. */
#define SDIO_TAIL_GRACE_MS 30U

/* Pre-scheduler bursts: NDTR must be unchanged this long before the tail is
 * pulled out of the FIFO (one 512-byte block moves in ~350 us at 12 MHz). */
#define SDIO_STALL_MS 3U

/* Enough to see the pattern in a serial log without turning a big copy into a
 * wall of text. */
#define SDIO_RETRY_LOG_LIMIT 8U

static uint32_t s_retry_logs = 0U;
static uint32_t s_tail_fixes = 0U;
static uint32_t s_dma_errors = 0U;

/* -------------------------------------------------------------------------- */
/* Hardware bring-up                                                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief  SDIO + GPIO clocks and pin mux (called before every HAL_SD_Init).
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
}

/**
 * @brief  Configure the RX stream and arm both interrupt lines.
 *
 *  Direct mode (no FIFO packing, single-beat bursts) on purpose: the tail that
 *  used to be stranded in the FIFO was one whole 4-beat burst, which FIFO mode
 *  stops requesting as soon as the data path closes.
 */
static GlobalType_t sdio_dma_init(void)
{
    __HAL_RCC_DMA2_CLK_ENABLE();

    s_hdma_rx.Instance                 = SDIO_DMA_STREAM;
    s_hdma_rx.Init.Channel             = SDIO_DMA_CHANNEL;
    s_hdma_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    s_hdma_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    s_hdma_rx.Init.MemInc              = DMA_MINC_ENABLE;
    s_hdma_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    s_hdma_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;
    s_hdma_rx.Init.Mode                = DMA_NORMAL;
    s_hdma_rx.Init.Priority            = DMA_PRIORITY_VERY_HIGH;
    s_hdma_rx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    s_hdma_rx.Init.FIFOThreshold       = DMA_FIFO_THRESHOLD_FULL;
    s_hdma_rx.Init.MemBurst            = DMA_MBURST_SINGLE;
    s_hdma_rx.Init.PeriphBurst         = DMA_PBURST_SINGLE;

    __HAL_LINKDMA(&hsd_card, hdmarx, s_hdma_rx);

    (void)HAL_DMA_DeInit(&s_hdma_rx);
    if (HAL_DMA_Init(&s_hdma_rx) != HAL_OK)
    {
        return RT_FAIL;
    }

    HAL_NVIC_SetPriority(SDIO_DMA_IRQn, SDIO_IRQ_PRIO, 0U);
    HAL_NVIC_EnableIRQ(SDIO_DMA_IRQn);
    HAL_NVIC_SetPriority(SDIO_IRQn, SDIO_IRQ_PRIO, 0U);
    HAL_NVIC_EnableIRQ(SDIO_IRQn);

    return RT_OK;
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

    if (s_dma_sem == NULL)
    {
        s_dma_sem = xSemaphoreCreateBinary();
        if (s_dma_sem == NULL)
        {
            return RT_FAIL;
        }
    }

    if (HAL_SD_Init(&hsd_card) != HAL_OK)
    {
        s_ready = 0;
        return RT_FAIL;
    }

    if (sdio_dma_init() != RT_OK)
    {
        HAL_SD_DeInit(&hsd_card);
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
    return RT_OK;
}

void bsp_sdio_deinit(void)
{
    HAL_SD_DeInit(&hsd_card);
    (void)HAL_DMA_DeInit(&s_hdma_rx);
    s_ready = 0;
}

uint8_t bsp_sdio_is_ready(void)
{
    return s_ready;
}

/* -------------------------------------------------------------------------- */
/* Interrupt entry points (called from app/stm32f4xx_it.c)                    */
/* -------------------------------------------------------------------------- */

void bsp_sdio_dma_rx_irq_handler(void)
{
    HAL_DMA_IRQHandler(&s_hdma_rx);
}

static void sdio_wake_task_from_isr(void)
{
    BaseType_t woken = pdFALSE;

    if ((s_dma_sem != NULL) &&
        (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED))
    {
        (void)xSemaphoreGiveFromISR(s_dma_sem, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

/**
 * @brief  Pull the words the stream never picked up out of the SDIO FIFO.
 *
 *  The end of a burst is the one moment this driver has to second-guess the
 *  peripheral: SDIO deasserts the DMA request when its data path closes, so the
 *  stream stops a few words short (measured: exactly 4 words / 16 bytes of a
 *  512-byte block) and NDTR never reaches 0.  Those words sit in the RX FIFO
 *  and SDIO->FIFOCNT does NOT report them (it reads 0), so they can only be
 *  recovered by reading the FIFO blindly.  Verified on hardware: the recovered
 *  tail carried the MBR signature (AA55 at 510/511), i.e. real data.
 *
 *  Timing matters: this must happen BEFORE HAL_SD_IRQHandler() clears
 *  DCTRL.DTEN, which flushes the FIFO for good.
 *
 * @param  left  NDTR, i.e. words still missing
 * @retval 1 tail recovered (or nothing was missing), 0 real truncation
 */
static uint8_t sdio_take_tail(uint8_t *buf, uint32_t bytes, uint32_t left)
{
    uint32_t *dst;

    if (left == 0U)
    {
        return 1U;
    }

    if ((buf == NULL) || (left > 32U))
    {
        /* More than the FIFO can hold: the card really stopped early. */
        return 0U;
    }

    dst = ((uint32_t *)(void *)buf) + ((bytes / 4U) - left);
    while (left-- > 0U)
    {
        *dst++ = hsd_card.Instance->FIFO;
    }

    return 1U;
}

void bsp_sdio_irq_handler(void)
{
    if (((hsd_card.Context & SD_CONTEXT_DMA) != 0U) &&
        (__HAL_SD_GET_FLAG(&hsd_card, SDIO_FLAG_DATAEND) != RESET))
    {
        /* Card finished. If the stream is short, the remainder is still in the
         * FIFO right now -- take it before the HAL flushes it away. */
        if (s_cur_buf != NULL)
        {
            if (sdio_take_tail((uint8_t *)(void *)s_cur_buf, s_cur_bytes,
                               s_hdma_rx.Instance->NDTR) != 0U)
            {
                if (s_hdma_rx.Instance->NDTR != 0U)
                {
                    s_tail_fixes++;
                }
                s_need_abort = 1U;
                s_dma_done   = SDIO_DMA_OK;
            }
            else
            {
                s_dma_errors++;
                s_dma_done = SDIO_DMA_ERR;
            }
            sdio_wake_task_from_isr();
        }

        s_dataend_ms = HAL_GetTick();
        s_dataend    = 1U;
    }

    HAL_SD_IRQHandler(&hsd_card);
}

/* HAL callbacks, reached from the DMA stream ISR. */
void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)
{
    (void)hsd;
    s_dma_done = SDIO_DMA_OK;
    sdio_wake_task_from_isr();
}

void HAL_SD_ErrorCallback(SD_HandleTypeDef *hsd)
{
    (void)hsd;
    s_dma_done = SDIO_DMA_ERR;
    sdio_wake_task_from_isr();
}

/* -------------------------------------------------------------------------- */
/* Read path                                                                  */
/* -------------------------------------------------------------------------- */

/**
 * @brief  One DMA burst into @p buf.
 */
static HAL_StatusTypeDef sdio_read_chunk(uint8_t *buf, uint32_t start_block,
                                         uint32_t nblocks)
{
    uint32_t start_ms;
    uint32_t bytes = nblocks * SDIO_BLOCK_SIZE;
    uint8_t  in_task;
    uint32_t stuck_left = 0xFFFFFFFFU;
    uint32_t stuck_ms   = 0U;

    if ((s_dma_sem == NULL) || (s_ready == 0U))
    {
        return HAL_ERROR;
    }

    s_dma_done   = SDIO_DMA_IDLE;
    s_dataend    = 0U;
    s_dataend_ms = 0U;
    s_need_abort = 0U;
    s_cur_bytes  = bytes;
    s_cur_buf    = buf; /* the ISR needs this if the stream ends up short */
    (void)xSemaphoreTake(s_dma_sem, 0); /* drop a stale give */

    if (HAL_SD_ReadBlocks_DMA(&hsd_card, buf, start_block, nblocks) != HAL_OK)
    {
        s_cur_buf = NULL;
        return HAL_ERROR;
    }

    /* Before vTaskStartScheduler() the kernel keeps BASEPRI at
     * configMAX_SYSCALL_INTERRUPT_PRIORITY, which masks both of our interrupts
     * (priority 6), so nothing ever sets s_dma_done and blocking on the
     * semaphore is illegal anyway.  Drive the burst straight from the registers
     * in that phase instead; the boot mount is the only user of it. */
    in_task  = (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) ? 1U : 0U;
    start_ms = HAL_GetTick();

    for (;;)
    {
        uint32_t left;

        if (s_dma_done != SDIO_DMA_IDLE)
        {
            break;
        }

        if (in_task != 0U)
        {
            (void)xSemaphoreTake(s_dma_sem, pdMS_TO_TICKS(1));
            if (s_dma_done != SDIO_DMA_IDLE)
            {
                break;
            }

            if ((s_dataend != 0U) &&
                ((HAL_GetTick() - s_dataend_ms) >= SDIO_TAIL_GRACE_MS))
            {
                /* The card finished but bsp_sdio_irq_handler() did not claim
                 * the burst (it would have set s_dma_done).  The HAL has
                 * already cleared DTEN by now, so the tail is gone: do not
                 * pretend this succeeded, let the retry logic replay it. */
                break;
            }
        }
        else
        {
            left = s_hdma_rx.Instance->NDTR;

            if (left == 0U)
            {
                /* Stream counted everything down; the completion interrupt is
                 * masked, so close the burst here. */
                s_need_abort = 1U;
                s_dma_done   = SDIO_DMA_OK;
                break;
            }

            /* The stream stops a few words short at the end of every burst, so
             * "NDTR has not moved for a few ms" is the end-of-burst signal here
             * (a live burst changes NDTR continuously). */
            if (left != stuck_left)
            {
                stuck_left = left;
                stuck_ms   = HAL_GetTick();
            }

            if ((left <= 32U) &&
                ((HAL_GetTick() - stuck_ms) >= SDIO_STALL_MS))
            {
                if (sdio_take_tail(buf, bytes, left) != 0U)
                {
                    s_tail_fixes++;
                    s_need_abort = 1U;
                    s_dma_done   = SDIO_DMA_OK;
                }
                else
                {
                    s_dma_errors++;
                    s_dma_done = SDIO_DMA_ERR;
                }
                break;
            }
        }

        if ((HAL_GetTick() - start_ms) >= SDIO_RW_TIMEOUT_MS)
        {
            break; /* HAL_TIMEOUT below */
        }
    }

    s_cur_buf = NULL;

    if (s_dma_done == SDIO_DMA_OK)
    {
        if (s_need_abort != 0U)
        {
            /* The ISR recovered the tail itself, so SD_DMAReceiveCplt() never
             * ran: close the data path (DPSM, DMAEN, CMD12 when multi-block)
             * and put hsd_card.State back to READY. */
            (void)HAL_SD_Abort(&hsd_card);
        }
        /* Else the HAL's own completion path already did all of the above. */
        return HAL_OK;
    }

    /* Tears down DPSM/DMAEN and emits CMD12 when the card is still mid-transfer
     * (every multi-block read), then puts hsd_card.State back to READY. */
    (void)HAL_SD_Abort(&hsd_card);

    return (s_dma_done == SDIO_DMA_ERR) ? HAL_ERROR : HAL_TIMEOUT;
}

/* -------------------------------------------------------------------------- */
/* Write path (polled)                                                        */
/* -------------------------------------------------------------------------- */

static HAL_StatusTypeDef sdio_write_chunk(const uint8_t *buf, uint32_t start_block,
                                          uint32_t nblocks)
{
    HAL_StatusTypeDef st;
    uint32_t          waited = 0U;

    st = HAL_SD_WriteBlocks(&hsd_card, (uint8_t *)(void *)buf, start_block,
                            nblocks, SDIO_RW_TIMEOUT_MS);
    if (st != HAL_OK)
    {
        return st;
    }

    /* The polled transfer is complete once the data counter runs out, but the
     * card itself may still be busy (write-back / internal state machine).
     * Waiting here keeps the next command from being issued too early. */
    while (HAL_SD_GetCardState(&hsd_card) != HAL_SD_CARD_TRANSFER)
    {
        if (waited++ >= SDIO_RW_TIMEOUT_MS)
        {
            return HAL_TIMEOUT;
        }
        HAL_Delay(1U);
    }

    return HAL_OK;
}

/* -------------------------------------------------------------------------- */
/* Chunked transfer with retry                                                */
/* -------------------------------------------------------------------------- */

static HAL_StatusTypeDef sdio_transfer(const uint8_t *buf, uint32_t start_block,
                                       uint32_t nblocks, bool write)
{
    if (!s_ready)
    {
        return HAL_ERROR;
    }

    while (nblocks > 0U)
    {
        uint32_t chunk = (nblocks > SDIO_CHUNK_BLOCKS) ? SDIO_CHUNK_BLOCKS : nblocks;
        uint32_t tries = 0U;
        HAL_StatusTypeDef st;

        for (;;)
        {
            st = (write != false)
                     ? sdio_write_chunk(buf, start_block, chunk)
                     : sdio_read_chunk((uint8_t *)(void *)buf, start_block, chunk);
            if (st == HAL_OK)
            {
                break;
            }

            /* One line per failed attempt, but only the first few: a copy run
             * can produce several, and a blocking UART print in the middle of
             * it would cost more than the retry.  Log before the abort, which
             * clears ErrorCode. */
            if (s_retry_logs < SDIO_RETRY_LOG_LIMIT)
            {
                s_retry_logs++;
                PRINT_LOG("[SD  ] %s blk=%lu n=%lu st=%d err=0x%08lX retry=%lu\r\n",
                          (write != false) ? "write" : "read",
                          (unsigned long)start_block, (unsigned long)chunk, (int)st,
                          (unsigned long)hsd_card.ErrorCode, (unsigned long)(tries + 1U));
            }

            if (write != false)
            {
                /* The read path aborts inside sdio_read_chunk(); writes do not. */
                (void)HAL_SD_Abort(&hsd_card);
                HAL_Delay(2U);
            }

            tries++;
            if (tries > SDIO_IO_RETRIES)
            {
                return st;
            }
        }

        buf += chunk * SDIO_BLOCK_SIZE;
        start_block += chunk;
        nblocks -= chunk;
    }

    return HAL_OK;
}

HAL_StatusTypeDef bsp_sdio_read_blocks(uint8_t *buf, uint32_t start_block, uint32_t nblocks)
{
    return sdio_transfer(buf, start_block, nblocks, false);
}

HAL_StatusTypeDef bsp_sdio_write_blocks(const uint8_t *buf, uint32_t start_block, uint32_t nblocks)
{
    return sdio_transfer(buf, start_block, nblocks, true);
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

void bsp_sdio_stats(uint32_t *tail_fixes, uint32_t *dma_errors, uint32_t *retry_logs)
{
    if (tail_fixes != NULL)
    {
        *tail_fixes = s_tail_fixes;
    }
    if (dma_errors != NULL)
    {
        *dma_errors = s_dma_errors;
    }
    if (retry_logs != NULL)
    {
        *retry_logs = s_retry_logs;
    }
}
