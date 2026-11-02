/**
 * @file    bsp_sdio.c
 * @brief   microSD card driver over SDIO (4-bit wide bus): DMA reads, polled writes.
 * @note    尾 FIFO 滞留与重试策略: document/stm32f429/video_player_port.md A5
 */
#include "bsp_sdio.h"

#include <stdbool.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "log.h"

SD_HandleTypeDef hsd_card;

/* -------------------------------------------------------------------------- */
/* DMA receive channel                                                        */
/* -------------------------------------------------------------------------- */

#define SDIO_DMA_STREAM DMA2_Stream6
#define SDIO_DMA_CHANNEL DMA_CHANNEL_4
#define SDIO_DMA_IRQn DMA2_Stream6_IRQn

/* Both at prio 6, i.e. below configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY:
 * the callbacks may use the FromISR API. */
#define SDIO_IRQ_PRIO 6U

/* Written by the ISRs, cleared by the task before every burst. */
#define SDIO_DMA_IDLE 0U
#define SDIO_DMA_OK 1U
#define SDIO_DMA_ERR 2U

/* DMA channel + the burst currently in flight. */
typedef struct
{
    DMA_HandleTypeDef hdma;       /* receive channel, linked via __HAL_LINKDMA */
    SemaphoreHandle_t sem;        /* given by the transfer-complete ISR */
    volatile uint8_t  done;       /* SDIO_DMA_IDLE / OK / ERR */
    volatile uint8_t  need_abort; /* ISR took the tail, task must still close */
    volatile uint8_t *buf;        /* burst in flight, NULL when idle */
    volatile uint32_t bytes;      /* nominal size of that burst */
} SDIO_DMA_STATE_T;

/* DATAEND bookkeeping: tells "card finished, stream short" from "still running". */
typedef struct
{
    volatile uint8_t  flag;
    volatile uint32_t ms;
} SDIO_DATAEND_T;

/* -------------------------------------------------------------------------- */
/* Tunables                                                                   */
/* -------------------------------------------------------------------------- */

/* 8 KB per burst: DMA drains the FIFO, so long bursts are safe; the cap
 * only bounds the latency of one retry. */
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

/* Run-time counters, read by bsp_sdio_get_stats(). */
typedef struct
{
    uint32_t retry_logs;
    uint32_t tail_fixes;
    uint32_t dma_errors;
} SDIO_STAT_T;

/* Everything the driver owns. */
typedef struct
{
    SDIO_DMA_STATE_T dma;
    SDIO_DATAEND_T   dataend;
    SDIO_STAT_T      stat;
    uint8_t          ready; /* card enumerated */
} SDIO_STATE_T;

static SDIO_STATE_T s_sdio = {0};

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

    s_sdio.dma.hdma.Instance                 = SDIO_DMA_STREAM;
    s_sdio.dma.hdma.Init.Channel             = SDIO_DMA_CHANNEL;
    s_sdio.dma.hdma.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    s_sdio.dma.hdma.Init.PeriphInc           = DMA_PINC_DISABLE;
    s_sdio.dma.hdma.Init.MemInc              = DMA_MINC_ENABLE;
    s_sdio.dma.hdma.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    s_sdio.dma.hdma.Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;
    s_sdio.dma.hdma.Init.Mode                = DMA_NORMAL;
    s_sdio.dma.hdma.Init.Priority            = DMA_PRIORITY_VERY_HIGH;
    s_sdio.dma.hdma.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    s_sdio.dma.hdma.Init.FIFOThreshold       = DMA_FIFO_THRESHOLD_FULL;
    s_sdio.dma.hdma.Init.MemBurst            = DMA_MBURST_SINGLE;
    s_sdio.dma.hdma.Init.PeriphBurst         = DMA_PBURST_SINGLE;

    __HAL_LINKDMA(&hsd_card, hdmarx, s_sdio.dma.hdma);

    (void)HAL_DMA_DeInit(&s_sdio.dma.hdma);
    if (HAL_DMA_Init(&s_sdio.dma.hdma) != HAL_OK)
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

    if (s_sdio.dma.sem == NULL)
    {
        s_sdio.dma.sem = xSemaphoreCreateBinary();
        if (s_sdio.dma.sem == NULL)
        {
            return RT_FAIL;
        }
    }

    if (HAL_SD_Init(&hsd_card) != HAL_OK)
    {
        s_sdio.ready = 0;
        return RT_FAIL;
    }

    if (sdio_dma_init() != RT_OK)
    {
        HAL_SD_DeInit(&hsd_card);
        s_sdio.ready = 0;
        return RT_FAIL;
    }

    /* Enumerated in 1-bit; widen to 4-bit for the data phase. */
    if (HAL_SD_ConfigWideBusOperation(&hsd_card, SDIO_BUS_WIDE_4B) != HAL_OK)
    {
        HAL_SD_DeInit(&hsd_card);
        s_sdio.ready = 0;
        return RT_FAIL;
    }

    s_sdio.ready = 1;
    return RT_OK;
}

void bsp_sdio_deinit(void)
{
    HAL_SD_DeInit(&hsd_card);
    (void)HAL_DMA_DeInit(&s_sdio.dma.hdma);
    s_sdio.ready = 0;
}

uint8_t bsp_sdio_is_ready(void)
{
    return s_sdio.ready;
}

/* -------------------------------------------------------------------------- */
/* Interrupt entry points (called from app/stm32f4xx_it.c)                    */
/* -------------------------------------------------------------------------- */

void bsp_sdio_dma_rx_irq_handler(void)
{
    HAL_DMA_IRQHandler(&s_sdio.dma.hdma);
}

static void sdio_wake_task_from_isr(void)
{
    BaseType_t woken = pdFALSE;

    if ((s_sdio.dma.sem != NULL) &&
        (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED))
    {
        (void)xSemaphoreGiveFromISR(s_sdio.dma.sem, &woken);
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
        if (s_sdio.dma.buf != NULL)
        {
            if (sdio_take_tail((uint8_t *)(void *)s_sdio.dma.buf, s_sdio.dma.bytes,
                               s_sdio.dma.hdma.Instance->NDTR) != 0U)
            {
                if (s_sdio.dma.hdma.Instance->NDTR != 0U)
                {
                    s_sdio.stat.tail_fixes++;
                }
                s_sdio.dma.need_abort = 1U;
                s_sdio.dma.done       = SDIO_DMA_OK;
            }
            else
            {
                s_sdio.stat.dma_errors++;
                s_sdio.dma.done = SDIO_DMA_ERR;
            }
            sdio_wake_task_from_isr();
        }

        s_sdio.dataend.ms   = HAL_GetTick();
        s_sdio.dataend.flag = 1U;
    }

    HAL_SD_IRQHandler(&hsd_card);
}

/* HAL callbacks, reached from the DMA stream ISR. */
void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)
{
    (void)hsd;
    s_sdio.dma.done = SDIO_DMA_OK;
    sdio_wake_task_from_isr();
}

void HAL_SD_ErrorCallback(SD_HandleTypeDef *hsd)
{
    (void)hsd;
    s_sdio.dma.done = SDIO_DMA_ERR;
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

    if ((s_sdio.dma.sem == NULL) || (s_sdio.ready == 0U))
    {
        return HAL_ERROR;
    }

    s_sdio.dma.done       = SDIO_DMA_IDLE;
    s_sdio.dataend.flag   = 0U;
    s_sdio.dataend.ms     = 0U;
    s_sdio.dma.need_abort = 0U;
    s_sdio.dma.bytes      = bytes;
    s_sdio.dma.buf        = buf;             /* the ISR needs this if the stream ends up short */
    (void)xSemaphoreTake(s_sdio.dma.sem, 0); /* drop a stale give */

    if (HAL_SD_ReadBlocks_DMA(&hsd_card, buf, start_block, nblocks) != HAL_OK)
    {
        s_sdio.dma.buf = NULL;
        return HAL_ERROR;
    }

    /* Pre-scheduler: BASEPRI masks prio 6, so poll registers instead.
     * See document/stm32f429/video_player_port.md A5. */
    in_task  = (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) ? 1U : 0U;
    start_ms = HAL_GetTick();

    for (;;)
    {
        uint32_t left;

        if (s_sdio.dma.done != SDIO_DMA_IDLE)
        {
            break;
        }

        if (in_task != 0U)
        {
            (void)xSemaphoreTake(s_sdio.dma.sem, pdMS_TO_TICKS(1));
            if (s_sdio.dma.done != SDIO_DMA_IDLE)
            {
                break;
            }

            if ((s_sdio.dataend.flag != 0U) &&
                ((HAL_GetTick() - s_sdio.dataend.ms) >= SDIO_TAIL_GRACE_MS))
            {
                /* Tail already flushed by HAL: fail so the retry replays it.
                 * See document/stm32f429/video_player_port.md A5. */
                break;
            }
        }
        else
        {
            left = s_sdio.dma.hdma.Instance->NDTR;

            if (left == 0U)
            {
                /* Stream counted everything down; the completion interrupt is
                 * masked, so close the burst here. */
                s_sdio.dma.need_abort = 1U;
                s_sdio.dma.done       = SDIO_DMA_OK;
                break;
            }

            /* End of burst = NDTR stopped moving (a live burst keeps changing).
             * See document/stm32f429/video_player_port.md A5. */
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
                    s_sdio.stat.tail_fixes++;
                    s_sdio.dma.need_abort = 1U;
                    s_sdio.dma.done       = SDIO_DMA_OK;
                }
                else
                {
                    s_sdio.stat.dma_errors++;
                    s_sdio.dma.done = SDIO_DMA_ERR;
                }
                break;
            }
        }

        if ((HAL_GetTick() - start_ms) >= SDIO_RW_TIMEOUT_MS)
        {
            break; /* HAL_TIMEOUT below */
        }
    }

    s_sdio.dma.buf = NULL;

    if (s_sdio.dma.done == SDIO_DMA_OK)
    {
        if (s_sdio.dma.need_abort != 0U)
        {
            /* ISR took the tail, so Cplt never ran: close the data path here.
             * See document/stm32f429/video_player_port.md A5. */
            (void)HAL_SD_Abort(&hsd_card);
        }
        /* Else the HAL's own completion path already did all of the above. */
        return HAL_OK;
    }

    /* Tears down DPSM/DMAEN and emits CMD12 when the card is still mid-transfer
     * (every multi-block read), then puts hsd_card.State back to READY. */
    (void)HAL_SD_Abort(&hsd_card);

    return (s_sdio.dma.done == SDIO_DMA_ERR) ? HAL_ERROR : HAL_TIMEOUT;
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

    /* NDTR is done but the card may still be busy: wait for TRANSFER state. */
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
    if (!s_sdio.ready)
    {
        return HAL_ERROR;
    }

    while (nblocks > 0U)
    {
        uint32_t          chunk = (nblocks > SDIO_CHUNK_BLOCKS) ? SDIO_CHUNK_BLOCKS : nblocks;
        uint32_t          tries = 0U;
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

            /* First few failures only; log before Abort, which clears
             * ErrorCode.  See document/stm32f429/video_player_port.md A5. */
            if (s_sdio.stat.retry_logs < SDIO_RETRY_LOG_LIMIT)
            {
                s_sdio.stat.retry_logs++;
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

    if (!s_sdio.ready)
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
        *tail_fixes = s_sdio.stat.tail_fixes;
    }
    if (dma_errors != NULL)
    {
        *dma_errors = s_sdio.stat.dma_errors;
    }
    if (retry_logs != NULL)
    {
        *retry_logs = s_sdio.stat.retry_logs;
    }
}
