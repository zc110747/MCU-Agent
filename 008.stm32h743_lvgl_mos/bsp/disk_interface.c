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
#include "drv_sdio.h"
#include "drv_rtc.h"
#include <string.h>

#define SD_RUN_ERROR_TIMES  4

static uint8_t is_fdisk_error = 0;

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
int MMC_disk_status(void)
{
    if(is_fdisk_error == 1) {
        return RES_ERROR;
    }
    
    return RES_OK;
}

int MMC_disk_initialize(void)
{
    //
    return 0;
}

int MMC_disk_read(BYTE *buff, LBA_t sector, UINT count)
{
    uint8_t res = 0;
    uint8_t index = 0;
    
    // 检测到异常, 不在读取，直接报错
    if (is_fdisk_error == 1) {
        return RES_ERROR;
    }
    
    // 循环读取，避免误检测
    do
    {
        res = sdcard_read_disk(buff, sector, count);
        if (res != HAL_OK) {
            index++;
            drv_sdcard_init();
        }
        
        if (index == SD_RUN_ERROR_TIMES)
        {
            is_fdisk_error = 1;
        }
    }while(res != HAL_OK && index < SD_RUN_ERROR_TIMES);
    
    if (res != HAL_OK) {
        return RES_ERROR;
    }
    return RES_OK;
}

int MMC_disk_write(const BYTE *buff, LBA_t sector, UINT count)
{
    uint8_t res = 0;
    uint8_t index = 0;
    
    // 检测到异常, 不在写入，直接报错
    if (is_fdisk_error == 1) {
        return RES_ERROR;
    }
    
    // 循环写入，避免误检测
    do
    {
        res = sdcard_write_disk(buff, sector, count);
        if (res != HAL_OK)
        {
            index++;
            drv_sdcard_init();
        }
        
        if (index == SD_RUN_ERROR_TIMES)
        {
            is_fdisk_error = 1;
        }
    }while(res != HAL_OK && index < SD_RUN_ERROR_TIMES);

    if (res != HAL_OK) {
        return RES_ERROR;
    }
    return RES_OK;
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

/* No USB mass-storage host on this board: the OTG core is wired as a CDC
 * device (see bsp/drv_usb_cdc.c).  The stubs stay so FF_VOLUMES can keep the
 * upstream drive numbering, they just never succeed. */
int USB_disk_read(BYTE *buff, LBA_t sector, UINT count)
{
    (void)buff;
    (void)sector;
    (void)count;
    return RES_NOTRDY;
}

int USB_disk_write(const BYTE *buff, LBA_t sector, UINT count)
{
    (void)buff;
    (void)sector;
    (void)count;
    return RES_NOTRDY;
}

/**
  * @brief  Timestamp for files FatFs creates, in the packed DOS format.
  *
  * Wired to the RTC so ROM directory listings and anything the firmware writes
  * carry a sensible date instead of 1980-01-01.  Falls back to the epoch the
  * format allows when the calendar is not running.
  */
DWORD get_fattime(void)
{
    rtc_datetime_t dt;

    if (drv_rtc_get(&dt) != RT_OK)
    {
        return ((DWORD)(2025 - 1980) << 25) | ((DWORD)1 << 21) | ((DWORD)1 << 16);
    }

    return ((DWORD)(dt.year - 1980) << 25)
         | ((DWORD)dt.month         << 21)
         | ((DWORD)dt.day           << 16)
         | ((DWORD)dt.hour          << 11)
         | ((DWORD)dt.minute        <<  5)
         | ((DWORD)(dt.second / 2U) <<  0);
}
