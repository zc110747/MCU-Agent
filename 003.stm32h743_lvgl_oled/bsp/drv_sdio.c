/**
  ******************************************************************************
  * @file    drv_sdio.c
  * @brief   SDMMC1 block device layer used by FatFs.
  *
 *  Buffers handed to HAL_SD_ReadBlocks()/WriteBlocks() are moved by the CPU
 *  draining the SDMMC FIFO (polling path, NOT the SDMMC internal DMA), so the
 *  D-Cache can stay enabled with no coherency hazard.  All RAM lives in
 *  AXI-SRAM at 0x24000000 by linker design.  If this driver is later switched
 *  to HAL_SD_ReadBlocks_DMA(), that IDMA master cannot reach DTCM, so the DMA
 *  buffer must be placed in AXI-SRAM / SRAM_D2 / SRAM4 and marked
 *  non-cacheable by the MPU.
  ******************************************************************************
  */
#include "drv_sdio.h"
#include "ff.h"
#include <string.h>
#include <stdio.h>

/**
  * @brief  (Re)initialise the SD card after a transfer error.
  */
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
    hsd1.Init.ClockDiv            = 4;

    if (HAL_SD_Init(&hsd1) != HAL_OK)
    {
        return RT_FAIL;
    }

    if (HAL_SD_ConfigWideBusOperation(&hsd1, SDMMC_BUS_WIDE_4B) != HAL_OK)
    {
        return RT_FAIL;
    }

    return RT_OK;
}

/**
  * @brief  Wait until the card leaves the programming/receiving state.
  */
static HAL_StatusTypeDef sdcard_wait_ready(void)
{
    uint32_t tickstart = HAL_GetTick();

    while (HAL_SD_GetCardState(&hsd1) != HAL_SD_CARD_TRANSFER)
    {
        if ((HAL_GetTick() - tickstart) > SDMMC_READ_WRITE_TIMEOUT)
        {
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
    if (sdcard_wait_ready() != HAL_OK)
    {
        return HAL_TIMEOUT;
    }

    status = HAL_SD_ReadBlocks(&hsd1, buf, startBlocks, NumberOfBlocks,
                               SDMMC_READ_WRITE_TIMEOUT);
    if (status != HAL_OK)
    {
        return status;
    }

    return sdcard_wait_ready();
}

HAL_StatusTypeDef sdcard_write_disk(const uint8_t *buf, uint32_t startBlocks,
                                    uint32_t NumberOfBlocks)
{
    HAL_StatusTypeDef status;

    if (sdcard_wait_ready() != HAL_OK)
    {
        return HAL_TIMEOUT;
    }

    status = HAL_SD_WriteBlocks(&hsd1, (uint8_t *)buf, startBlocks,
                                NumberOfBlocks, SDMMC_READ_WRITE_TIMEOUT);
    if (status != HAL_OK)
    {
        return status;
    }

    return sdcard_wait_ready();
}

/*----------------------------------------------------------------------------
 *  Capacity reporting
 *--------------------------------------------------------------------------*/

/* FF_MIN_SS == FF_MAX_SS in ffconf.h, so the sector size is a compile-time
 * constant and FATFS has no ssize member to read it from. */
#if FF_MAX_SS == FF_MIN_SS
  #define SD_SECTOR_SIZE(fs)    ((uint32_t)FF_MIN_SS)
#else
  #define SD_SECTOR_SIZE(fs)    ((uint32_t)(fs)->ssize)
#endif

GlobalType_t drv_sd_query_info(sd_info_t *info)
{
    HAL_SD_CardInfoTypeDef ci;
    FATFS                 *fs   = NULL;
    DWORD                  free_clusters = 0;
    uint32_t               sector;

    if (info == NULL)
    {
        return RT_FAIL;
    }
    memset(info, 0, sizeof(*info));

    /* Physical size straight from the CSD - available even if the card holds
     * no filesystem we can read. */
    if (HAL_SD_GetCardInfo(&hsd1, &ci) == HAL_OK)
    {
        info->card_type  = (uint8_t)ci.CardType;
        info->card_bytes = (uint64_t)ci.LogBlockNbr * (uint64_t)ci.LogBlockSize;
    }

    if ((f_getfree(SD_VOLUME_PATH, &free_clusters, &fs) != FR_OK) || (fs == NULL))
    {
        return RT_FAIL;
    }

    sector = SD_SECTOR_SIZE(fs);

    /* n_fatent counts the two reserved FAT entries, hence the -2. */
    info->fs_type        = fs->fs_type;
    info->fs_total_bytes = (uint64_t)(fs->n_fatent - 2U) *
                           (uint64_t)fs->csize * (uint64_t)sector;
    info->fs_free_bytes  = (uint64_t)free_clusters *
                           (uint64_t)fs->csize * (uint64_t)sector;
    info->valid          = 1U;

    return RT_OK;
}

void drv_sd_format_size(uint64_t bytes, char *out, uint32_t out_len)
{
    static const char *const unit[5] = { "B", "KB", "MB", "GB", "TB" };
    uint32_t idx   = 0U;
    uint64_t whole = bytes;
    uint32_t frac  = 0U;

    if ((out == NULL) || (out_len == 0U))
    {
        return;
    }

    while ((whole >= 1024U) && (idx < 4U))
    {
        frac   = (uint32_t)(((whole % 1024U) * 10U) / 1024U);
        whole /= 1024U;
        idx++;
    }

    /* whole is < 1024 here, so plain unsigned long formatting is enough and
     * we never need the (unlinked) long long printf path. */
    if (idx == 0U)
    {
        (void)snprintf(out, out_len, "%lu B", (unsigned long)whole);
    }
    else
    {
        (void)snprintf(out, out_len, "%lu.%lu %s",
                       (unsigned long)whole, (unsigned long)frac, unit[idx]);
    }
}

const char *drv_sd_fs_name(uint8_t fs_type)
{
    switch (fs_type)
    {
        case FS_FAT12: return "FAT12";
        case FS_FAT16: return "FAT16";
        case FS_FAT32: return "FAT32";
        case FS_EXFAT: return "exFAT";
        default:       return "-";
    }
}

const char *drv_sd_card_name(uint8_t card_type)
{
    switch (card_type)
    {
        case CARD_SDSC:      return "SDSC";
        case CARD_SDHC_SDXC: return "SDHC/SDXC";
        default:             return "?";
    }
}
