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

#include <stdio.h>

SD_HandleTypeDef hsd_card;

static int s_ready = 0;

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

GlobalType_t bsp_sdio_init(void)
{
    sdio_msp_init();

    hsd_card.Instance           = SDIO;
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

int bsp_sdio_is_ready(void)
{
    return s_ready;
}

HAL_StatusTypeDef bsp_sdio_read_blocks(uint8_t *buf, uint32_t start_block, uint32_t nblocks)
{
    HAL_StatusTypeDef st;
    uint32_t waited = 0;

    if (!s_ready)
    {
        return HAL_ERROR;
    }

    st = HAL_SD_ReadBlocks(&hsd_card, buf, start_block, nblocks, SDIO_RW_TIMEOUT_MS);
    if (st != HAL_OK)
    {
        return st;
    }

    /* The polled transfer is complete once RxXferCplt sets, but the card itself
     * may still be busy (write-back / internal state machine).  Wait until it
     * reports TRANSFER so the next command is not issued too early. */
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

HAL_StatusTypeDef bsp_sdio_write_blocks(const uint8_t *buf, uint32_t start_block, uint32_t nblocks)
{
    HAL_StatusTypeDef st;
    uint32_t waited = 0;

    if (!s_ready)
    {
        return HAL_ERROR;
    }

    st = HAL_SD_WriteBlocks(&hsd_card, (uint8_t *)buf, start_block, nblocks,
                            SDIO_RW_TIMEOUT_MS);
    if (st != HAL_OK)
    {
        return st;
    }

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
