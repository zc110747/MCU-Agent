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

GLOBAL_TYPE_T sd_card_remount(void)
{
    FRESULT rc;

    if (s_mounted == 0)
    {
        return sd_card_init();
    }

    /* force = 1: drop the cached BPB/FAT/FSInfo and read them again.  The
     * FATFS instance and therefore every open FIL stay valid. */
    fs_lock();
    rc = f_mount(&s_sd_fs, FS_VOL_SD, 1);
    fs_unlock();

    if (rc != FR_OK)
    {
        PRINT_LOG("[SD  ] remount failed rc=%d\r\n", (int32_t)rc);
        s_mounted = 0U;
        return RT_FAIL;
    }
    return RT_OK;
}

GLOBAL_TYPE_T sd_card_usage_mb(uint32_t *total_mb, uint32_t *used_mb)
{
    uint32_t cluster_bytes;
    uint32_t total_bytes;
    uint32_t total_clusters;
    uint32_t free_clusters;
    FRESULT  rc;

    if (total_mb != NULL)
    {
        *total_mb = 0U;
    }
    if (used_mb != NULL)
    {
        *used_mb = 0U;
    }
    if (s_mounted == 0)
    {
        return RT_FAIL;
    }

    /* FF_MIN_SS == FF_MAX_SS == 512, so the sector size is a compile-time
     * constant and fs->ssize does not even exist in this configuration. */
    cluster_bytes  = (uint32_t)s_sd_fs.csize * 512U;
    total_clusters = (uint32_t)s_sd_fs.n_fatent - 2U;
    total_bytes    = total_clusters * cluster_bytes;
    free_clusters  = (uint32_t)s_sd_fs.free_clst;

    if (free_clusters > total_clusters)
    {
        /* Value is 0xFFFFFFFF (or stale): the FSInfo/FSInfo-less volume did not
         * give us a free count.
         *
         * f_getfree() would now walk the whole FAT.  On a 16 GB card with a
         * dirty FSInfo that is ~16 MB of sectors -- tens of seconds of polled
         * SDIO, which would both hang this call and make the USB host time out.
         * So only take that path where it is bounded: FAT12/16 tables are at
         * most a couple of hundred KB (<= 64K clusters here), FAT32 is not.
         * Reporting "unknown" is better than stalling the hand-over. */
        if (total_clusters > 65536U)
        {
            return RT_FAIL;
        }

        {
            DWORD nclst = 0U;

            fs_lock();
            rc = f_getfree(FS_VOL_SD, &nclst, NULL);
            fs_unlock();

            if ((rc != FR_OK) || ((uint32_t)nclst > total_clusters))
            {
                return RT_FAIL;
            }
            free_clusters = (uint32_t)nclst;
        }
    }

    if (total_mb != NULL)
    {
        *total_mb = total_bytes >> 20;
    }
    if (used_mb != NULL)
    {
        *used_mb = (total_bytes - (free_clusters * cluster_bytes)) >> 20;
    }
    return RT_OK;
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
