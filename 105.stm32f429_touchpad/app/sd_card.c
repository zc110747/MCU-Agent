/**
 ******************************************************************************
 * @file    sd_card.c
 * @brief   microSD bring-up: SDIO peripheral + FatFs volume "0:".
 *
 *  The two steps always go together -- the peripheral must be enumerated before
 *  FatFs may touch the volume:
 *      bsp_sdio_init()      -> hardware + card identification
 *      f_mount(&fs, "0:", 1) -> filesystem
 *
 *  This is the *only* storage the application has: the GBK glyph files, the
 *  background image and the icon set all live on it.
 ******************************************************************************
 */
#include <stdio.h>

#include "ff.h"

#include "sd_card.h"
#include "fs_diskio.h"
#include "log.h"

static FATFS   s_sd_fs;
static uint8_t s_mounted = 0U;

GlobalType_t sd_card_init(void)
{
    FRESULT  rc;
    uint32_t blocks = 0U;
    uint16_t bsize  = 512U;

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

GlobalType_t sd_card_usage_mb(uint32_t *total_mb, uint32_t *used_mb)
{
    uint32_t cluster_bytes;
    uint32_t total_clusters;
    uint32_t free_clusters;
    uint64_t total_bytes;
    uint64_t free_bytes;
    FRESULT  rc;

    if (total_mb != NULL) { *total_mb = 0U; }
    if (used_mb  != NULL) { *used_mb  = 0U; }
    if (s_mounted == 0)
    {
        return RT_FAIL;
    }

    /* FF_MIN_SS == FF_MAX_SS == 512, so the sector size is a compile-time
     * constant and fs->ssize does not even exist in this configuration. */
    cluster_bytes  = (uint32_t)s_sd_fs.csize * 512U;
    total_clusters = (uint32_t)s_sd_fs.n_fatent - 2U;
    free_clusters  = (uint32_t)s_sd_fs.free_clst;

    /* Byte counts MUST be 64-bit: a 32 GB FAT32 card has ~1.9M clusters of
     * 16 KB, i.e. ~3.0e10 bytes.  In uint32_t that wrapped modulo 4 GiB and a
     * 29.7 GB card was reported as 1744 MB (with a bogus 136 MB "used"). */
    total_bytes = (uint64_t)total_clusters * (uint64_t)cluster_bytes;

    if ((free_clusters > total_clusters) || (free_clusters == 0U))
    {
        /* 0xFFFFFFFF (or stale) means the FSInfo did not give us a count.
         * f_getfree() would walk the whole FAT: tens of seconds of polled SDIO
         * on a large card.  Only take that path when the FAT is small. */
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

    free_bytes = (uint64_t)free_clusters * (uint64_t)cluster_bytes;

    /* >> 20 needs no 64-bit division (only __aeabi_lshift), so this stays
     * cheap even with the soft-float library in use. */
    if (total_mb != NULL) { *total_mb = (uint32_t)(total_bytes >> 20); }
    if (used_mb  != NULL) { *used_mb  = (uint32_t)((total_bytes - free_bytes) >> 20); }
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
