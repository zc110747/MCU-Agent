/**
 ******************************************************************************
 * @file    bsp_sdio.c
 * @brief   microSD card driver over SDIO (4-bit wide bus), polled (no DMA).
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
 ******************************************************************************
 */
#include "bsp_sdio.h"

#include <stdbool.h>
#include <stdio.h>

#include "log.h"

SD_HandleTypeDef hsd_card;

static uint8_t s_ready = 0U;

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

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

GLOBAL_TYPE_T bsp_sdio_init(void)
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

/* Polled transfers and interrupts do not mix: the 32-word RX FIFO overruns if
 * the CPU is away for more than ~10 us, and with the USB device stack up there
 * is always another interrupt waiting to happen.  A burst that lost that race
 * also leaves the data path stalled, so one collision used to poison every
 * later read until the volume was remounted.
 *
 * Two countermeasures, both inside the existing polled design:
 *   - cap each HAL burst at 8 blocks (4 KB).  Shorter bursts mean a smaller
 *     window in which an ISR can steal the CPU.
 *   - on failure, HAL_SD_Abort() to tear the data path down and retry the same
 *     chunk.  Reads are idempotent, so a retry is always safe. */
#define SDIO_CHUNK_BLOCKS 8U
#define SDIO_IO_RETRIES 3U

/* Enough to see the pattern on a scope/serial log without turning a large copy
 * into a wall of text. */
#define SDIO_RETRY_LOG_LIMIT 8U
static uint32_t s_retry_logs = 0U;

/**
 * @brief  One burst, plus the wait for the card to report TRANSFER again.
 */
static HAL_StatusTypeDef sdio_transfer_chunk(uint8_t *buf, uint32_t start_block,
                                             uint32_t nblocks, bool write)
{
    HAL_StatusTypeDef st;
    uint32_t          waited = 0U;

    if (write)
    {
        st = HAL_SD_WriteBlocks(&hsd_card, buf, start_block, nblocks, SDIO_RW_TIMEOUT_MS);
    }
    else
    {
        st = HAL_SD_ReadBlocks(&hsd_card, buf, start_block, nblocks, SDIO_RW_TIMEOUT_MS);
    }

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
        HAL_Delay(1);
    }

    return HAL_OK;
}

static HAL_StatusTypeDef sdio_transfer(uint8_t *buf, uint32_t start_block,
                                       uint32_t nblocks, bool write)
{
    HAL_StatusTypeDef st = HAL_OK;

    if (!s_ready)
    {
        return HAL_ERROR;
    }

    while (nblocks > 0U)
    {
        uint32_t chunk = (nblocks > SDIO_CHUNK_BLOCKS) ? SDIO_CHUNK_BLOCKS : nblocks;
        uint32_t tries = 0U;

        for (;;)
        {
            st = sdio_transfer_chunk(buf, start_block, chunk, write);
            if (st == HAL_OK)
            {
                break;
            }

            /* One line per failed attempt, but only the first few: a copy run
             * produces a collision every few hundred KB, and a blocking UART
             * print in the middle of it would cost more than the retry.  The
             * error code is still meaningful here -- HAL_SD_Abort() clears it. */
            if (s_retry_logs < SDIO_RETRY_LOG_LIMIT)
            {
                s_retry_logs++;
                PRINT_LOG("[SD  ] %s blk=%lu n=%lu st=%d err=0x%08lX retry=%lu\r\n",
                          (write != false) ? "write" : "read",
                          (unsigned long)start_block, (unsigned long)chunk, (int)st,
                          (unsigned long)hsd_card.ErrorCode, (unsigned long)(tries + 1U));
            }

            (void)HAL_SD_Abort(&hsd_card);
            HAL_Delay(2U);

            tries++;
            if (tries >= SDIO_IO_RETRIES)
            {
                return st;
            }
        }

        buf += chunk * 512U;
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
    return sdio_transfer((uint8_t *)buf, start_block, nblocks, true);
}

GLOBAL_TYPE_T bsp_sdio_get_info(uint32_t *block_count, uint16_t *block_size)
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
