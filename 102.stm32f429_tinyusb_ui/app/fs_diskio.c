/**
  ******************************************************************************
  * @file    fs_diskio.c
  * @brief   Combined FatFs diskio glue: pdrv 0 = USB MSC, pdrv 1 = microSD.
  *
  *  USB path (pdrv 0)
  *  -----------------
  *  Mirrors TinyUSB's reference msc_file_explorer example: disk_read/write
  *  submit a SCSI READ10/WRITE10 and BLOCK until the completion callback
  *  (fired inside tuh_task) clears the busy flag.  That is why usbh_host_task
  *  and any task doing filesystem work must run concurrently.
  *
  *  SD path (pdrv 1)
  *  ----------------
  *  Straight HAL_SD polled transfers.  The SDIO FIFO is read through a
  *  uint32_t pointer inside the HAL, so the destination MUST be 4-byte
  *  aligned.  FatFs normally hands us a sector buffer that already is, but
  *  f_read() has a direct-transfer fast path that can pass the caller's own
  *  buffer through.  Anything unaligned is bounced through s_sd_scratch.
  ******************************************************************************
  */
#include <string.h>

#include "ff.h"
#include "diskio.h"

#include "tusb.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "fs_diskio.h"
#include "bsp_sdio.h"

/* -------------------------------------------------------------------------- */
/* FatFs serialization                                                        */
/* -------------------------------------------------------------------------- */
static SemaphoreHandle_t s_fs_lock = NULL;

int fs_diskio_init(void)
{
    s_fs_lock = xSemaphoreCreateMutex();
    return (s_fs_lock != NULL) ? 1 : 0;
}

void fs_lock(void)
{
    if (s_fs_lock != NULL)
    {
        (void)xSemaphoreTake(s_fs_lock, portMAX_DELAY);
    }
}

void fs_unlock(void)
{
    if (s_fs_lock != NULL)
    {
        (void)xSemaphoreGive(s_fs_lock);
    }
}

/* -------------------------------------------------------------------------- */
/* USB MSC transport (pdrv 0)                                                 */
/* -------------------------------------------------------------------------- */
static volatile bool s_usb_busy = false;

static void wait_for_usb_disk_io(void)
{
    while (s_usb_busy)
    {
        vTaskDelay(1);
    }
}

static bool usb_disk_io_complete(uint8_t dev_addr, const tuh_msc_complete_data_t *cb_data)
{
    (void)dev_addr;
    (void)cb_data;
    s_usb_busy = false;
    return true;
}

/* -------------------------------------------------------------------------- */
/* microSD transport (pdrv 1)                                                 */
/* -------------------------------------------------------------------------- */

/* 4-byte aligned bounce buffer for unaligned callers.  One sector is enough:
 * the font reader asks for <= 128 bytes at a time anyway. */
static uint8_t s_sd_scratch[SDIO_BLOCK_SIZE] __attribute__((aligned(4)));

/* -------------------------------------------------------------------------- */
/* FatFs diskio API                                                           */
/* -------------------------------------------------------------------------- */

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv == FS_DRV_USB)
    {
        /* TinyUSB hands out device addresses starting at 1. */
        return tuh_msc_mounted((uint8_t)(pdrv + 1u)) ? (DSTATUS)0 : STA_NODISK;
    }

    if (pdrv == FS_DRV_SD)
    {
        return bsp_sdio_is_ready() ? (DSTATUS)0 : STA_NODISK;
    }

    return STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv == FS_DRV_USB)
    {
        return (DSTATUS)0;   /* nothing to do; TinyUSB owns the transport */
    }

    if (pdrv == FS_DRV_SD)
    {
        /* The card is brought up by app/sd_card.c before f_mount().  If we get
         * here without a card, say so instead of pretending. */
        return bsp_sdio_is_ready() ? (DSTATUS)0 : STA_NODISK;
    }

    return STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv == FS_DRV_USB)
    {
        const uint8_t dev_addr = (uint8_t)(pdrv + 1u);

        s_usb_busy = true;
        tuh_msc_read10(dev_addr, 0, buff, sector, (uint16_t)count,
                       usb_disk_io_complete, 0);
        wait_for_usb_disk_io();
        return RES_OK;
    }

    if (pdrv == FS_DRV_SD)
    {
        if (count == 0u)
        {
            return RES_PARERR;
        }

        if ((((uint32_t)buff) & 3u) == 0u)
        {
            /* Already word aligned: hand the caller's buffer straight to the
             * HAL and push all sectors in one transfer. */
            if (bsp_sdio_read_blocks(buff, sector, count) != HAL_OK)
            {
                return RES_ERROR;
            }
            return RES_OK;
        }

        /* Unaligned destination: bounce sector by sector. */
        for (UINT i = 0u; i < count; i++)
        {
            if (bsp_sdio_read_blocks(s_sd_scratch, (uint32_t)(sector + i), 1u) != HAL_OK)
            {
                return RES_ERROR;
            }
            memcpy(buff + ((uint32_t)i * SDIO_BLOCK_SIZE), s_sd_scratch, SDIO_BLOCK_SIZE);
        }
        return RES_OK;
    }

    return RES_PARERR;
}

#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv == FS_DRV_USB)
    {
        const uint8_t dev_addr = (uint8_t)(pdrv + 1u);

        s_usb_busy = true;
        tuh_msc_write10(dev_addr, 0, (uint8_t *)buff, sector, (uint16_t)count,
                        usb_disk_io_complete, 0);
        wait_for_usb_disk_io();
        return RES_OK;
    }

    if (pdrv == FS_DRV_SD)
    {
        if (count == 0u)
        {
            return RES_PARERR;
        }

        if ((((uint32_t)buff) & 3u) == 0u)
        {
            if (bsp_sdio_write_blocks(buff, sector, count) != HAL_OK)
            {
                return RES_ERROR;
            }
            return RES_OK;
        }

        for (UINT i = 0u; i < count; i++)
        {
            memcpy(s_sd_scratch, buff + ((uint32_t)i * SDIO_BLOCK_SIZE), SDIO_BLOCK_SIZE);
            if (bsp_sdio_write_blocks(s_sd_scratch, (uint32_t)(sector + i), 1u) != HAL_OK)
            {
                return RES_ERROR;
            }
        }
        return RES_OK;
    }

    return RES_PARERR;
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv == FS_DRV_USB)
    {
        const uint8_t dev_addr = (uint8_t)(pdrv + 1u);

        switch (cmd)
        {
            case CTRL_SYNC:
                return RES_OK;
            case GET_SECTOR_COUNT:
                *((DWORD *)buff) = (DWORD)tuh_msc_get_block_count(dev_addr, 0);
                return RES_OK;
            case GET_SECTOR_SIZE:
                *((WORD *)buff) = (WORD)tuh_msc_get_block_size(dev_addr, 0);
                return RES_OK;
            case GET_BLOCK_SIZE:
                *((DWORD *)buff) = 1;
                return RES_OK;
            default:
                return RES_PARERR;
        }
    }

    if (pdrv == FS_DRV_SD)
    {
        uint32_t blocks = 0u;
        uint16_t bsize  = (uint16_t)SDIO_BLOCK_SIZE;

        switch (cmd)
        {
            case CTRL_SYNC:
                return RES_OK;

            case GET_SECTOR_COUNT:
            case GET_SECTOR_SIZE:
                if (bsp_sdio_get_info(&blocks, &bsize) != RT_OK)
                {
                    return RES_ERROR;
                }
                if (cmd == GET_SECTOR_COUNT)
                {
                    *((DWORD *)buff) = (DWORD)blocks;
                }
                else
                {
                    *((WORD *)buff) = (WORD)bsize;
                }
                return RES_OK;

            case GET_BLOCK_SIZE:
                /* Erase block granularity is not used by this project. */
                *((DWORD *)buff) = 1;
                return RES_OK;

            default:
                return RES_PARERR;
        }
    }

    return RES_PARERR;
}
