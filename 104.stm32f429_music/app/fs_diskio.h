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
 *
 *  USB mass-storage sessions
 *  -------------------------
 *  When the board is plugged into a PC, the host owns the card: it reads and
 *  writes raw sectors and must see a stable filesystem, so NOTHING else may be
 *  inside FatFs or in the middle of a sector transfer.  The session API below
 *  expresses exactly that:
 *
 *      fs_usb_session_begin()   blocks until the current FatFs user is done,
 *                               then KEEPS the mutex -- every fs_lock() from
 *                               another task now blocks until the session ends.
 *      fs_usb_session_end()     hands the mutex back.
 *
 *  Consequences for callers:
 *    - fs_usb_session_begin() must not be called while holding fs_lock().
 *    - a task that may block for a long time (the audio stream) must notice the
 *      session through its own flag and park BEFORE the session starts; the
 *      blocked fs_lock() is only the last-resort safety net.
 *    - optional users (the CJK glyph loader) test fs_usb_session_active() and
 *      fail fast instead of blocking, so the UI task never freezes.
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

/**
 * @brief  Take the FatFs lock, waiting as long as it takes.
 *         Safe before fs_diskio_init() (no-op).
 * @note   Blocks for the whole duration of a USB storage session.
 */
void fs_lock(void);

/** Release the FatFs lock. */
void fs_unlock(void);

/**
 * @brief  Take the FatFs lock only if it is available within @p timeout_ms.
 * @retval 1 acquired (caller must fs_unlock()), 0 busy / session active
 * @note   For optional users that must never stall their task; the caller is
 *         expected to degrade gracefully when this returns 0.
 */
int fs_try_lock(uint32_t timeout_ms);

/**
 * @brief  Give the card exclusively to the USB mass-storage host.
 *
 *  Waits for the current FatFs user to finish and then holds the lock for the
 *  whole session.  Must NOT be called while the calling task holds fs_lock().
 */
void fs_usb_session_begin(void);

/** Hand the card back to the application (releases the lock). */
void fs_usb_session_end(void);

/** @retval 1 while a USB storage session owns the card. */
int fs_usb_session_active(void);

#ifdef __cplusplus
}
#endif

#endif /* FS_DISKIO_H */
