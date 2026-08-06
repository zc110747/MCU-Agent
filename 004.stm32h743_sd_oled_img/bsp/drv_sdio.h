/**
  ******************************************************************************
  * @file    drv_sdio.h
  * @brief   SDMMC1 block device + FatFs volume management.
  ******************************************************************************
  */

#ifndef __BSP_SDCARD_H
#define __BSP_SDCARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "ff.h"

/** Logical drive used for the SD card. FF_VOLUMES == 1, so this is "0:". */
#define SD_DRIVE_PATH        "1:"

/** Re-initialise the SDMMC peripheral / card. Called on recovery paths too. */
GlobalType_t drv_sdcard_init(void);

/** Initialise the card and mount the FatFs volume on SD_DRIVE_PATH. */
GlobalType_t bsp_sdcard_mount(void);

/** Unmount the volume (does not power down the card). */
void bsp_sdcard_unmount(void);

/** 1 when the FatFs volume is currently mounted. */
int bsp_sdcard_is_mounted(void);

/** Print card capacity / type / block size to the console. */
void bsp_sdcard_dump_info(void);

/* Raw block access used by the FatFs disk I/O layer -------------------------*/
HAL_StatusTypeDef bsp_sdcard_read_blocks(uint8_t *buf, uint32_t start_block, uint32_t nblocks);
HAL_StatusTypeDef bsp_sdcard_write_blocks(const uint8_t *buf, uint32_t start_block, uint32_t nblocks);
uint32_t          bsp_sdcard_block_count(void);
HAL_StatusTypeDef sdcard_read_disk(uint8_t *buf, uint32_t startBlocks,
                                   uint32_t NumberOfBlocks);
HAL_StatusTypeDef sdcard_write_disk(const uint8_t *buf, uint32_t startBlocks,
                                    uint32_t NumberOfBlocks);
#ifdef __cplusplus
}
#endif

#endif /* __BSP_SDCARD_H */
