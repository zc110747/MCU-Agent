/**
 ******************************************************************************
 * @file    fs_diskio.h
 * @brief   FatFs diskio glue for the single volume of this project.
 *
 *      pdrv 0  ->  "0:"  microSD card over SDIO (4-bit, polled)
 *
 *  FatFs itself is not re-entrant, and the SDIO peripheral is one stateful
 *  handle, so every call that touches the filesystem is wrapped in
 *  fs_lock()/fs_unlock().  The glyph loader (UI task, on demand) and the
 *  wallpaper/image loader share the card through that mutex.
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
#define FS_DRV_SD      0u
#define FS_VOL_SD      "0:"

/**
 * @brief  Create the FatFs serialization mutex.
 *         Must be called after the FreeRTOS heap is up and before any task
 *         touches the filesystem.
 * @retval 1 on success, 0 when the mutex could not be created.
 */
int fs_diskio_init(void);

/**
 * @brief  Take the FatFs lock, waiting as long as it takes.
 *         Safe before fs_diskio_init() (no-op).
 */
void fs_lock(void);

/** Release the FatFs lock. */
void fs_unlock(void);

/**
 * @brief  Take the FatFs lock only if it is available within @p timeout_ms.
 * @retval 1 acquired (caller must fs_unlock()), 0 busy
 * @note   For optional users (glyph loader) that must never stall the caller;
 *         they are expected to degrade gracefully when this returns 0.
 */
int fs_try_lock(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* FS_DISKIO_H */
