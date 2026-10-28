/*-----------------------------------------------------------------------*/
/* Low level disk I/O module SKELETON for FatFs     (C)ChaN, 2019        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/
#include "disk_interface.h"
#include "diskio.h"
#include "bsp_sdcard.h"
#include <string.h>

extern SD_HandleTypeDef hsd1;

//RAM disk
int RAM_disk_status(void)
{
    //for ram, status already ok.
    return 0;
}

int RAM_disk_initialize(void)
{
    //for ram, not need initialize
    return 0;
}

int RAM_disk_read(BYTE *buff, LBA_t sector, UINT count)
{
    uint32_t address = RAM_START_ADDRESS + sector*RAM_SECTOR_SIZE;
    uint32_t size = count * RAM_SECTOR_SIZE;
    
    memcpy(buff, (uint8_t *)address, size);
    return 0;
}

int RAM_disk_write(const BYTE *buff, LBA_t sector, UINT count)
{
    uint32_t address = RAM_START_ADDRESS + sector*RAM_SECTOR_SIZE;
    uint32_t size = count * RAM_SECTOR_SIZE;
    
    memcpy((uint8_t *)address, buff, size);
    return 0;
}

int RAM_disk_ioctl(BYTE cmd, void *buff)
{
    switch(cmd)
    {
        case GET_BLOCK_SIZE:
            *(DWORD *)buff = RAM_SECTOR_SIZE;
            break;
        case GET_SECTOR_COUNT:
            *(DWORD *)buff = RAM_SECTOR_COUNT;
            break;
        case CTRL_SYNC:
            break;
    }
    return 0;
}

//MMC disk
/* ------------------------------------------------------------------------ */
/* MMC / SD disk glue                                                       */
/*                                                                          */
/* The block layer in bsp_sdcard.c already waits for the card to become idle  */
/* and retries a failed transfer three times. Re-initialising the card from   */
/* here (as an earlier revision did) meant re-running the whole 400 kHz       */
/* identification sequence while the USB host was waiting for a SCSI answer - */
/* exactly the situation where Windows falls into a multi-second retry loop.  */
/* ------------------------------------------------------------------------ */
int MMC_disk_status(void)
{
    return sdcard_present() ? RES_OK : RES_NOTRDY;
}

int MMC_disk_initialize(void)
{
    /* Cheap fast path: the card is usually already up (main() init). Only run
       the full init when it is not, which doubles as hot-insert recovery. */
    if (!sdcard_present()) sdcard_init();
    return sdcard_present() ? RES_OK : RES_NOTRDY;
}

int MMC_disk_read(BYTE *buff, LBA_t sector, UINT count)
{
    return (sdcard_read_blocks(buff, (uint32_t) sector, (uint32_t) count) == SD_ST_OK)
           ? RES_OK : RES_ERROR;
}

int MMC_disk_write(const BYTE *buff, LBA_t sector, UINT count)
{
    return (sdcard_write_blocks(buff, (uint32_t) sector, (uint32_t) count) == SD_ST_OK)
           ? RES_OK : RES_ERROR;
}

int MMC_disk_ioctl(BYTE cmd, void *buff)
{
    DRESULT res = RES_OK;
    HAL_SD_CardInfoTypeDef info;

    switch(cmd)
    {
        case GET_BLOCK_SIZE:
            /* Erase block size expressed in sectors */
            *(DWORD *)buff = 1;
            break;
        case GET_SECTOR_SIZE:
            *(WORD *)buff = MMC_SECTOR_SIZE;
            break;
        case GET_SECTOR_COUNT:
            /* Ask the card rather than trusting a hard coded constant, so the
               same firmware works with any capacity. */
            if (HAL_SD_GetCardInfo(&hsd1, &info) == HAL_OK)
            {
                *(DWORD *)buff = (DWORD)info.LogBlockNbr;
            }
            else
            {
                *(DWORD *)buff = MMC_SECTOR_COUNT;
            }
            break;
        case CTRL_SYNC:
            break;
        default:
            res = RES_PARERR;
            break;
    }
    return res;
}

//USB disk
int USB_disk_status(void)
{
    return 0;
}

int USB_disk_initialize(void)
{
    return 0;
}

int USB_disk_read(BYTE *buff, LBA_t sector, UINT count)
{
    return 0;
}

int USB_disk_write(const BYTE *buff, LBA_t sector, UINT count)
{
    return 0;
}

DWORD get_fattime(void)
{
    return 0;
}
