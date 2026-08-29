/**
  ******************************************************************************
  * @file    fs_diskio.h
  * @brief   Combined FatFs diskio glue for the two volumes of this project.
  *
  *   pdrv 0  ->  "0:"  USB mass-storage device (TinyUSB host, MSC/SCSI)
  *   pdrv 1  ->  "1:"  microSD card (SDIO, 4-bit, polled)
  *
  *  Both volumes live behind a single FreeRTOS mutex (fs_lock/fs_unlock).
  *  That is not optional: the USB MSC transport uses one shared busy flag per
  *  LUN, so two tasks issuing I/O at the same time corrupt it (lost wakeup ->
  *  permanent spin).  The lock also protects the SDIO peripheral, which is a
  *  single stateful handle.
  ******************************************************************************
  */
#ifndef FS_DISKIO_H
#define FS_DISKIO_H

#include <stdint.h>

/* Physical drive numbers.  Kept in one place so the volume strings ("0:",
 * "1:") and the diskio switch always agree. */
#define FS_DRV_USB      0u
#define FS_DRV_SD       1u

/* Volume prefix strings, for callers that build FatFs paths. */
#define FS_VOL_USB      "0:"
#define FS_VOL_SD       "1:"

/**
  * @brief  Create the FatFs serialization mutex.
  *         Called from main() after the SDRAM heap is up and before any
  *         FreeRTOS object that touches the filesystem.
  * @retval 1 on success, 0 on failure
  */
int fs_diskio_init(void);

/**
  * @brief  Take / release the FatFs serialization mutex.
  *         Safe to call before fs_diskio_init() (they degrade to no-ops).
  */
void fs_lock(void);
void fs_unlock(void);

#endif /* FS_DISKIO_H */
