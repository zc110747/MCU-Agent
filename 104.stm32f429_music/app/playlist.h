/**
 ******************************************************************************
 * @file    playlist.h
 * @brief   Track list built by scanning the MUSIC folder on the SD card.
 *
 *  The reference player walked the FAT directory with internal FatFs helpers
 *  (dir_sdi) and re-read directory entries on every skip.  That couples the
 *  application to FatFs internals.  Here the folder is scanned once into a
 *  fixed-size table, and skipping is just an index change.
 ******************************************************************************
 */
#ifndef PLAYLIST_H
#define PLAYLIST_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLAYLIST_DIR "0:/MUSIC"
#define PLAYLIST_MAX_TRACKS 64U
#define PLAYLIST_NAME_MAX 96U

/**
 * @brief  Scan PLAYLIST_DIR and rebuild the track table (sorted by name).
 *         Takes the FatFs lock internally.
 * @retval true   at least one playable file (.wav / .mp3) was found
 * @retval false  folder missing / empty / unreadable
 */
bool playlist_scan(void);

/**
 * @brief  Drop the track table (count -> 0), forcing a rescan.
 *
 *  Used when the volume itself became unreadable: the caller unmounts and
 *  remounts the card, and the next playlist_count() == 0 makes audio_task
 *  scan the folder again instead of spinning on stale entries.
 */
void playlist_clear(void);

/** Number of playable tracks found by the last successful scan. */
uint16_t playlist_count(void);

/** Number of regular files in the folder, playable or not. */
uint16_t playlist_dir_files(void);

/** Current track index, or -1 when nothing is selected. */
int16_t playlist_current(void);

/** File name (no path) of a track, or NULL when the index is out of range. */
const char *playlist_name(uint16_t index);

/**
 * @brief  Move the current index by `delta`, wrapping at both ends.
 * @retval true when a track is selected afterwards.
 */
bool playlist_step(int16_t delta);

/** Select an absolute index (wrapped). */
bool playlist_select(uint16_t index);

/**
 * @brief  Build the full FatFs path of a track.
 * @retval true when the path fits in `out_size`.
 */
bool playlist_path(uint16_t index, char *out, uint32_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* PLAYLIST_H */
