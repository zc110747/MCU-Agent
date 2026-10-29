/**
 ******************************************************************************
 * @file    sd_card.c
 * @brief   microSD bring-up: SDIO peripheral + FatFs volume "0:".
 *
 *  The two steps always go together -- the peripheral must be enumerated before
 *  FatFs may touch the volume:
 *      bsp_sdio_init()  -> hardware + card identification
 *      f_mount(&fs, "0:") -> filesystem
 ******************************************************************************
 */
#include <stdio.h>

#include "ff.h"

#include "sd_card.h"
#include "fs_diskio.h"
#include "log.h"

static FATFS   s_sd_fs;
static uint8_t s_mounted = 0U;

GLOBAL_TYPE_T sd_card_init(void)
{
    FRESULT  rc;
    uint32_t blocks = 0U;
    uint16_t bsize  = 0U;

    if (s_mounted != 0)
    {
        return RT_OK;
    }

    if (bsp_sdio_init() != RT_OK)
    {
        PRINT_LOG("[SD  ] SDIO init FAILED (no card?)\r\n");
        return RT_FAIL;
    }

    (void)bsp_sdio_get_info(&blocks, &bsize);
    PRINT_LOG("[SD  ] SDIO init OK (%lu MB, block=%u)\r\n",
              (unsigned long)((blocks / 1024U) * (uint32_t)bsize / 1024U),
              (unsigned int)bsize);

    fs_lock();
    rc = f_mount(&s_sd_fs, FS_VOL_SD, 1);
    fs_unlock();

    if (rc != FR_OK)
    {
        PRINT_LOG("[SD  ] f_mount failed rc=%d\r\n", (int32_t)rc);
        return RT_FAIL;
    }

    s_mounted = 1U;
    PRINT_LOG("[SD  ] mounted " FS_VOL_SD "\r\n");
    return RT_OK;
}

void sd_card_invalidate(void)
{
    if (s_mounted != 0)
    {
        fs_lock();
        (void)f_mount(NULL, FS_VOL_SD, 0);
        fs_unlock();
        s_mounted = 0U;
    }
    bsp_sdio_deinit();
}

int sd_card_is_ready(void)
{
    return s_mounted;
}

uint32_t sd_card_capacity_mb(void)
{
    uint32_t blocks = 0U;
    uint16_t bsize  = 512U;

    if (s_mounted == 0)
    {
        return 0U;
    }
    if (bsp_sdio_get_info(&blocks, &bsize) != RT_OK)
    {
        return 0U;
    }
    return (uint32_t)((blocks / 1024U) * (uint32_t)bsize / 1024U);
}
