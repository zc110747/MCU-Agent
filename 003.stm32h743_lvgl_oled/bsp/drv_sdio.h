//////////////////////////////////////////////////////////////////////////////
//  (c) copyright 2023-by Persional Inc.  
//  All Rights Reserved
//
//  Name:
//      drv_sdio.h
//
//  Purpose:
//      driver for sdio module.
//
//  Author:
//      @公众号：<嵌入式技术总结>
//
//  Assumptions:
//	
//
//  Revision History:
//
/////////////////////////////////////////////////////////////////////////////
#ifndef _DRV_SDIO_H
#define _DRV_SDIO_H

#ifdef __cplusplus
extern "C" {
#endif
 
#include "main.h" 

#define SDIO_MODE_PULL                  0
#define SDIO_MODE_DMA                   1
#define SDIO_RUN_MODE                   SDIO_MODE_DMA

#define SDMMC_READ_WRITE_TIMEOUT        1000
#define SDMMC_BLOCK_SIZE                512
#define SDMMC_CLOCK_DIV                 2

#define _SDIO_DMA_SUPPORT               0

/* FatFs logical drive the card is mounted on (see drv_oled_text.c). */
#define SD_VOLUME_PATH                  "1:"

/**
  * @brief  Card + filesystem geometry, everything the UI needs in one shot.
  */
typedef struct
{
    uint8_t  valid;             /*!< 1 when the filesystem numbers are usable  */
    uint8_t  fs_type;           /*!< FatFs FS_FAT12/16/32, 0 = not mounted     */
    uint8_t  card_type;         /*!< CARD_SDSC / CARD_SDHC_SDXC                */
    uint64_t card_bytes;        /*!< physical capacity read from the CSD       */
    uint64_t fs_total_bytes;    /*!< usable filesystem size                    */
    uint64_t fs_free_bytes;     /*!< free space                                */
} sd_info_t;

GlobalType_t drv_sdcard_init(void);
HAL_StatusTypeDef sdcard_read_disk(uint8_t *buf, uint32_t startBlocks, uint32_t NumberOfBlocks);
HAL_StatusTypeDef sdcard_write_disk(const uint8_t *buf, uint32_t startBlocks, uint32_t NumberOfBlocks);

/**
  * @brief  Query card capacity and free space.
  * @note   f_getfree() walks the whole FAT the first time it runs on a FAT32
  *         volume (hundreds of ms on a large card).  Later calls are served
  *         from the cached free-cluster count, so this is cheap to poll once
  *         the first query is done - just do not call it from a tight loop.
  */
GlobalType_t drv_sd_query_info(sd_info_t *info);

/**
  * @brief  Human readable size, e.g. "29.7 GB".
  * @note   Integer only: newlib-nano is linked without float printf support.
  */
void drv_sd_format_size(uint64_t bytes, char *out, uint32_t out_len);

/** @brief  "FAT12" / "FAT16" / "FAT32" / "exFAT" / "-". */
const char *drv_sd_fs_name(uint8_t fs_type);

/** @brief  "SDSC" / "SDHC/SDXC" / "?". */
const char *drv_sd_card_name(uint8_t card_type);


#ifdef __cplusplus
}
#endif

#endif
