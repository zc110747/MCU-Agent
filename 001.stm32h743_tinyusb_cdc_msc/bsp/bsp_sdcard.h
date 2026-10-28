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
 *
 * Every block transfer is retried inside the driver (and preceded by a wait
 * for the card to leave the busy state), so a transient card error never
 * reaches the USB host as a failed SCSI command.
 * -------------------------------------------------------------------------*/
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "bsp.h"

typedef enum {
  SD_ST_OK      =  0,
  SD_ST_ERR     = -1,
  SD_ST_NO_CARD = -2,
} SD_STATUS_T;

/* ------------------------------------------------------------------------ */
/* Event trace                                                              */
/*                                                                          */
/* A small RAM ring of timestamped events (ms since reset). It answers the   */
/* only question that matters when a host takes ages to mount the disk:      */
/* what did the host ask for, when, and what did the card answer?            */
/* Dump it over the CDC port with the 'mtrace' command.                     */
/* ------------------------------------------------------------------------ */
#ifndef SD_TRACE_ENABLE
#define SD_TRACE_ENABLE 1
#endif

#define SD_TRACE_N 128

enum {
  SD_TR_BOOT = 1,   /* firmware reached main()                       */
  SD_TR_SDINIT,     /* SD init failed      code=stage sub=HAL error  */
  SD_TR_SDREADY,    /* SD ready            code=1/0 sub=block size   */
  SD_TR_FATFS,      /* FatFs mount         code=FRESULT              */
  SD_TR_TUSB,       /* tusb_init() done                              */
  SD_TR_MOUNT,      /* host configured us (SET_CONFIGURATION)        */
  SD_TR_UMOUNT,     /* host released us                              */
  SD_TR_TUR,        /* TEST UNIT READY     code=ready                */
  SD_TR_CAP,        /* READ CAPACITY       arg=blocks                */
  SD_TR_RD,         /* READ10              arg=lba                   */
  SD_TR_RDOK,       /* READ10 done         sub=bytes                 */
  SD_TR_RDERR,      /* READ10 failed       code=attempt sub=HAL err  */
  SD_TR_WR,         /* WRITE10             arg=lba                   */
  SD_TR_WROK,       /* WRITE10 done        sub=bytes                 */
  SD_TR_WRERR,      /* WRITE10 failed      code=attempt sub=HAL err  */
  SD_TR_SCSI,       /* SCSI command        code=opcode sub=bufsize   */
  SD_TR_SCSIUNK,    /* unsupported SCSI    code=opcode               */
  SD_TR_SSU,        /* START STOP UNIT     code=start sub=load_eject */
  SD_TR_WARM,       /* warm-up read        code=attempt arg=status   */
};

void sd_trace_add(uint8_t tag, uint8_t code, uint16_t sub, uint32_t arg);
void sd_trace_reset(void);

/* Formats the next trace line into out. Returns false when exhausted. */
bool sd_trace_next(char* out, int outsz, uint16_t* cursor);

/* Last HAL error code seen by the block layer (0 = none). */
uint32_t sdcard_last_error(void);

/* ------------------------------------------------------------------------ */
void     sdcard_init(void);
bool     sdcard_present(void);
uint32_t sdcard_block_count(void);   /* number of 512-byte logical blocks */
uint32_t sdcard_block_size(void);    /* 512 for SDSC/SDHC/SDXC           */

/* Returns SD_ST_OK on success, negative on failure. lba is the block index. */
int32_t sdcard_read_blocks(uint8_t* buf, uint32_t lba, uint32_t count);
int32_t sdcard_write_blocks(const uint8_t* buf, uint32_t lba, uint32_t count);
HAL_StatusTypeDef sdcard_read_disk(uint8_t *buf, uint32_t startBlocks,
                                   uint32_t NumberOfBlocks);
HAL_StatusTypeDef sdcard_write_disk(const uint8_t *buf, uint32_t startBlocks,
                                    uint32_t NumberOfBlocks);

#ifdef __cplusplus
}
#endif
