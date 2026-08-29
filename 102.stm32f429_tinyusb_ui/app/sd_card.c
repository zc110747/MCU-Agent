/**
  ******************************************************************************
  * @file    sd_card.c
  * @brief   microSD card bring-up: SDIO peripheral + FatFs volume "1:".
  *
  *  Ordering
  *  --------
  *  The peripheral must be up before FatFs may touch the volume, so the two
  *  steps are always paired here:
  *      bsp_sdio_init()          -> hardware + card identification
  *      f_mount(&s_sd_fs, "1:")  -> filesystem (exFAT/FAT32)
  *
  *  All FatFs work is wrapped in fs_lock()/fs_unlock() so the USB volume's
  *  file task and the UI task can never be inside FatFs at the same time.
  ******************************************************************************
  */
#include <stdio.h>

#include "ff.h"

#include "sd_card.h"
#include "fs_diskio.h"
#include "bsp_sdio.h"
#include "log.h"

static FATFS  s_sd_fs;
static int    s_mounted = 0;

/* When the loader keeps re-probing an empty socket it must not flood the
 * console, so the "no card" line can be silenced after the first report. */
static int    s_quiet = 0;

void sd_card_set_quiet(int on)
{
    s_quiet = on ? 1 : 0;
}

GlobalType_t sd_card_init(void)
{
    FRESULT   rc;
    uint32_t  blocks = 0u;
    uint16_t  bsize  = 0u;

    if (s_mounted != 0)
    {
        return RT_OK;
    }

    if (bsp_sdio_init() != RT_OK)
    {
        if (s_quiet == 0)
        {
            PRINT_LOG("[SD  ] SDIO init FAILED (no card?)\r\n");
        }
        return RT_FAIL;
    }

    (void)bsp_sdio_get_info(&blocks, &bsize);
    PRINT_LOG("[SD  ] SDIO init OK  (%lu MB, block=%u)\r\n",
           (unsigned long)((blocks / 1024u) * (uint32_t)bsize / 1024u),
           (unsigned int)bsize);

    fs_lock();
    rc = f_mount(&s_sd_fs, FS_VOL_SD, 1);
    fs_unlock();

    if (rc != FR_OK)
    {
        PRINT_LOG("[SD  ] f_mount(\"" FS_VOL_SD "\") failed rc=%d\r\n", (int)rc);
        return RT_FAIL;
    }

    s_mounted = 1;
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
        s_mounted = 0;
    }
    bsp_sdio_deinit();
}

int sd_card_is_ready(void)
{
    return s_mounted;
}

uint32_t sd_card_capacity_mb(void)
{
    uint32_t blocks = 0u;
    uint16_t bsize  = 512u;

    if (s_mounted == 0)
    {
        return 0u;
    }
    if (bsp_sdio_get_info(&blocks, &bsize) != RT_OK)
    {
        return 0u;
    }
    return (uint32_t)((blocks / 1024u) * (uint32_t)bsize / 1024u);
}
