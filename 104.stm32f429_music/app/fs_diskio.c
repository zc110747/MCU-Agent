/**
 ******************************************************************************
 * @file    fs_diskio.c
 * @brief   FatFs <-> SDIO glue for volume "0:".
 *
 *  The SDIO data FIFO is drained through a uint32_t pointer inside the HAL, so
 *  every buffer handed to HAL_SD_ReadBlocks() must be 4-byte aligned.  FatFs
 *  normally passes its own aligned sector buffer, but f_read() has a direct
 *  path that can forward the caller's buffer, so unaligned requests are bounced
 *  through s_sd_scratch.
 *
 *  Polled transfers only: the card is read once per audio buffer, the cost is
 *  negligible and it keeps the driver callable from any task without ISR
 *  plumbing.
 ******************************************************************************
 */
#include <string.h>

#include "ff.h"
#include "diskio.h"

#include "FreeRTOS.h"
#include "semphr.h"

#include "fs_diskio.h"
#include "bsp_sdio.h"

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

/* Bounce buffer for unaligned callers.  One sector is enough: unaligned reads
 * only come from short f_read() bursts, never from the audio stream (which uses
 * a word-aligned DMA buffer). */
static uint8_t s_sd_scratch[SDIO_BLOCK_SIZE] __attribute__((aligned(4)));

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != FS_DRV_SD)
    {
        return STA_NOINIT;
    }
    return bsp_sdio_is_ready() ? (DSTATUS)0 : STA_NODISK;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != FS_DRV_SD)
    {
        return STA_NOINIT;
    }
    /* The card is brought up by app/sd_card.c before f_mount(); if we get here
     * without a card, say so instead of pretending. */
    return bsp_sdio_is_ready() ? (DSTATUS)0 : STA_NODISK;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != FS_DRV_SD)
    {
        return RES_PARERR;
    }
    if ((buff == NULL) || (count == 0U))
    {
        return RES_PARERR;
    }

    if ((((uint32_t)buff) & 3U) == 0U)
    {
        if (bsp_sdio_read_blocks(buff, (uint32_t)sector, count) != HAL_OK)
        {
            return RES_ERROR;
        }
        return RES_OK;
    }

    for (UINT i = 0U; i < count; i++)
    {
        if (bsp_sdio_read_blocks(s_sd_scratch, (uint32_t)(sector + i), 1U) != HAL_OK)
        {
            return RES_ERROR;
        }
        memcpy(buff + ((uint32_t)i * SDIO_BLOCK_SIZE), s_sd_scratch, SDIO_BLOCK_SIZE);
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != FS_DRV_SD)
    {
        return RES_PARERR;
    }
    if ((buff == NULL) || (count == 0U))
    {
        return RES_PARERR;
    }

    if ((((uint32_t)buff) & 3U) == 0U)
    {
        if (bsp_sdio_write_blocks(buff, (uint32_t)sector, count) != HAL_OK)
        {
            return RES_ERROR;
        }
        return RES_OK;
    }

    for (UINT i = 0U; i < count; i++)
    {
        memcpy(s_sd_scratch, buff + ((uint32_t)i * SDIO_BLOCK_SIZE), SDIO_BLOCK_SIZE);
        if (bsp_sdio_write_blocks(s_sd_scratch, (uint32_t)(sector + i), 1U) != HAL_OK)
        {
            return RES_ERROR;
        }
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    uint32_t blocks = 0U;
    uint16_t bsize  = (uint16_t)SDIO_BLOCK_SIZE;

    if (pdrv != FS_DRV_SD)
    {
        return RES_PARERR;
    }

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
        /* Erase-block granularity is not used here. */
        *((DWORD *)buff) = 1U;
        return RES_OK;

    default:
        return RES_PARERR;
    }
}
