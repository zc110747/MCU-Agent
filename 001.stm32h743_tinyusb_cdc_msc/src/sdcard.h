/* ---------------------------------------------------------------------------
 * SD card driver - STM32H743 SDMMC1
 *
 * Pin mapping on the 鹿小班 board:
 *   PC8  = SDMMC1_D0      PC9  = SDMMC1_D1
 *   PC10 = SDMMC1_D2      PC11 = SDMMC1_D3
 *   PC12 = SDMMC1_CK      PD2  = SDMMC1_CMD
 *
 * The card is accessed through a simple 512-byte block read/write interface
 * (see sdcard_read_blocks / sdcard_write_blocks) that both FatFs (diskio) and
 * the USB Mass Storage class build on top of. Polling mode only - no DMA, no
 * SDMMC interrupt required.
 * -------------------------------------------------------------------------*/
#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
  SD_ST_OK      =  0,
  SD_ST_ERR     = -1,
  SD_ST_NO_CARD = -2,
} sd_status_t;

void     sdcard_init(void);
bool     sdcard_present(void);
uint32_t sdcard_block_count(void);   /* number of 512-byte logical blocks */
uint32_t sdcard_block_size(void);    /* 512 for SDSC/SDHC/SDXC           */

/* Returns SD_ST_OK on success, negative on failure. lba is the block index. */
int sdcard_read_blocks(uint8_t* buf, uint32_t lba, uint32_t count);
int sdcard_write_blocks(const uint8_t* buf, uint32_t lba, uint32_t count);
