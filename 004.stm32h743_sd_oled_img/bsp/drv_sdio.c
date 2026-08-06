/**
  ******************************************************************************
  * @file    bsp_sdcard.c
  * @brief   SDMMC1 block device + FatFs volume management.
  *
  * Transfer mode
  * -------------
  * HAL_SD_ReadBlocks()/HAL_SD_WriteBlocks() are the *polling* variants: the CPU
  * moves data byte-by-byte through the SDMMC FIFO. That means
  *   - no D-Cache maintenance is required (no DMA writes behind the cache),
  *   - the destination buffer needs no particular alignment,
  *   - the buffer may live in DTCM (which SDMMC IDMA could not reach).
  * Throughput is around 5-8 MB/s, which is far more than a 5s slideshow needs.
  ******************************************************************************
  */

#include "drv_sdio.h"
#include "bsp_log.h"

#include "diskio.h"

#define SD_OP_TIMEOUT_MS      2000U
#define SD_READY_TIMEOUT_MS   1000U

static FATFS s_fatfs;
static int   s_mounted = 0;

/* -------------------------------------------------------------------------- */
/* Low level                                                                   */
/* -------------------------------------------------------------------------- */
GlobalType_t drv_sdcard_init(void)
{
    if (HAL_SD_DeInit(&hsd1) != HAL_OK)
    {
        return RT_FAIL;
    }

    hsd1.Instance                 = SDMMC1;
    hsd1.Init.ClockEdge           = SDMMC_CLOCK_EDGE_RISING;
    hsd1.Init.ClockPowerSave      = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    hsd1.Init.BusWide             = SDMMC_BUS_WIDE_4B;
    hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
    hsd1.Init.ClockDiv            = 6;

    if (HAL_SD_Init(&hsd1) != HAL_OK) {
        /* No card inserted is not fatal: the app reports it on screen. */
        return RT_FAIL;
    }

    return RT_OK;
}

/**
  * @brief  Wait until the card returns to the transfer state.
  */
static HAL_StatusTypeDef sd_wait_ready(void)
{
    uint32_t tick = HAL_GetTick();

    while (HAL_SD_GetCardState(&hsd1) != HAL_SD_CARD_TRANSFER) {
        if ((HAL_GetTick() - tick) > SD_READY_TIMEOUT_MS) {
            return HAL_TIMEOUT;
        }
    }
    return HAL_OK;
}

HAL_StatusTypeDef sdcard_read_disk(uint8_t *buf, uint32_t startBlocks,
                                   uint32_t NumberOfBlocks)
{
    HAL_StatusTypeDef status;

    /* Card must be idle before a new command */
    if (sd_wait_ready() != HAL_OK)
    {
        return HAL_TIMEOUT;
    }

    status = HAL_SD_ReadBlocks(&hsd1, buf, startBlocks, NumberOfBlocks,
                               SD_OP_TIMEOUT_MS);
    if (status != HAL_OK)
    {
        return status;
    }

    return sd_wait_ready();
}

HAL_StatusTypeDef sdcard_write_disk(const uint8_t *buf, uint32_t startBlocks,
                                    uint32_t NumberOfBlocks)
{
    HAL_StatusTypeDef status;

    if (sd_wait_ready() != HAL_OK)
    {
        return HAL_TIMEOUT;
    }

    status = HAL_SD_WriteBlocks(&hsd1, (uint8_t *)buf, startBlocks,
                                NumberOfBlocks, SD_OP_TIMEOUT_MS);
    if (status != HAL_OK)
    {
        return status;
    }

    return sd_wait_ready();
}

HAL_StatusTypeDef bsp_sdcard_read_blocks(uint8_t *buf, uint32_t start_block, uint32_t nblocks)
{
    HAL_StatusTypeDef status;

    status = HAL_SD_ReadBlocks(&hsd1, buf, start_block, nblocks, SD_OP_TIMEOUT_MS);
    if (status != HAL_OK) {
        return status;
    }
    return sd_wait_ready();
}

HAL_StatusTypeDef bsp_sdcard_write_blocks(const uint8_t *buf, uint32_t start_block, uint32_t nblocks)
{
    HAL_StatusTypeDef status;

    status = HAL_SD_WriteBlocks(&hsd1, (uint8_t *)buf, start_block, nblocks, SD_OP_TIMEOUT_MS);
    if (status != HAL_OK) {
        return status;
    }
    return sd_wait_ready();
}

uint32_t bsp_sdcard_block_count(void)
{
    return hsd1.SdCard.LogBlockNbr;
}

/* -------------------------------------------------------------------------- */
/* Volume management                                                           */
/* -------------------------------------------------------------------------- */

GlobalType_t bsp_sdcard_mount(void)
{
    FRESULT res;

    s_mounted = 0;

    bsp_sdcard_dump_info();

    /* opt = 1 -> mount immediately so errors surface here, not at f_open() */
    res = f_mount(&s_fatfs, SD_DRIVE_PATH, 1);
    if (res != FR_OK) {
        LOG_E("f_mount(%s) failed, FRESULT=%d", SD_DRIVE_PATH, res);
        return RT_FAIL;
    }

    s_mounted = 1;
    LOG_I("FatFs volume %s mounted", SD_DRIVE_PATH);
    return RT_OK;
}

void bsp_sdcard_unmount(void)
{
    f_mount(NULL, SD_DRIVE_PATH, 0);
    s_mounted = 0;
}

int bsp_sdcard_is_mounted(void)
{
    return s_mounted;
}

void bsp_sdcard_dump_info(void)
{
    HAL_SD_CardInfoTypeDef info = {0};
    uint64_t               bytes;

    if (HAL_SD_GetCardInfo(&hsd1, &info) != HAL_OK) {
        LOG_W("HAL_SD_GetCardInfo failed");
        return;
    }

    bytes = (uint64_t)info.LogBlockNbr * (uint64_t)info.LogBlockSize;

    LOG_I("SD card: type=%lu, blocks=%lu, blocksize=%lu, capacity=%lu MB",
          (unsigned long)info.CardType,
          (unsigned long)info.LogBlockNbr,
          (unsigned long)info.LogBlockSize,
          (unsigned long)(bytes >> 20));
}
