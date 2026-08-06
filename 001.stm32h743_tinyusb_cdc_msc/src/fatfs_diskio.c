/* ---------------------------------------------------------------------------
 * FatFs disk I/O layer for the SD card.
 *
 * These four functions are the only thing FatFs needs; they delegate to the
 * polling block read/write interface in sdcard.c. The same sdcard_*
 * functions are used by the USB Mass Storage class, so the device-side
 * filesystem (FatFs) and the host-side U-disk always see identical sectors.
 * -------------------------------------------------------------------------*/

#include "ff.h"
#include "diskio.h"
#include "sdcard.h"

static DSTATUS g_stat = STA_NOINIT;

DSTATUS disk_initialize(BYTE pdrv) {
  (void) pdrv;
  if (!sdcard_present()) return STA_NODISK;
  g_stat &= ~STA_NOINIT;
  return g_stat;
}

DSTATUS disk_status(BYTE pdrv) {
  (void) pdrv;
  if (!sdcard_present()) return STA_NOINIT | STA_NODISK;
  return g_stat;
}

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
  (void) pdrv;
  if (!sdcard_present())            return RES_NOTRDY;
  if (sdcard_read_blocks(buff, (uint32_t) sector, (uint32_t) count) != SD_ST_OK)
    return RES_ERROR;
  return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
  (void) pdrv;
  if (!sdcard_present())                 return RES_NOTRDY;
  if (sdcard_write_blocks(buff, (uint32_t) sector, (uint32_t) count) != SD_ST_OK)
    return RES_ERROR;
  return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
  (void) pdrv;
  switch (cmd) {
    case CTRL_SYNC:
      return RES_OK;
    case GET_SECTOR_COUNT:
      *((DWORD*) buff) = sdcard_block_count();
      return RES_OK;
    case GET_SECTOR_SIZE:
      *((WORD*) buff) = (WORD) sdcard_block_size();
      return RES_OK;
    case GET_BLOCK_SIZE:
      *((DWORD*) buff) = 1;     /* erase block size in sectors (unknown -> 1) */
      return RES_OK;
    default:
      return RES_PARERR;
  }
}
