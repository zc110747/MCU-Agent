/**
 ******************************************************************************
 * @file    fs_diskio.h
 * @brief   FatFs diskio glue for the single volume of this project.
 *
 *   pdrv 0  ->  "0:"  microSD card over SDIO (4-bit, polled)
 *
 *  FatFs itself is not re-entrant.  Every call that touches the filesystem is
 *  therefore wrapped in fs_lock()/fs_unlock(), so the audio task (which streams
 *  WAV data) and the UI task (which lists the MUSIC folder) can never be inside
 *  it at the same time.
 ******************************************************************************
 */
#ifndef FS_DISKIO_H
#define FS_DISKIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Physical drive number and the matching FatFs volume prefix.  FF_VOLUMES is 1,
 * so "0:" is the only mountable volume. */
#define FS_DRV_SD 0u
#define FS_VOL_SD "0:"

/**
 * @brief  Create the FatFs serialization mutex.
 *         Must be called after the FreeRTOS heap is up and before any task
 *         touches the filesystem.
 * @retval 1 on success, 0 when the mutex could not be created.
 */
int fs_diskio_init(void);

/** Take / release the FatFs lock.  Safe before fs_diskio_init() (no-op). */
void fs_lock(void);
void fs_unlock(void);

#ifdef __cplusplus
}
#endif

#endif /* FS_DISKIO_H */
