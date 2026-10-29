/**
 ******************************************************************************
 * @file    playlist.c
 * @brief   Builds the track table from PLAYLIST_DIR ("0:/MUSIC").
 *
 *  Only *.wav is accepted (case-insensitive).  Names are copied out of FILINFO
 *  immediately because f_readdir() reuses its buffer, and the table is sorted
 *  with a simple insertion sort -- 64 entries at most, done once per rescan.
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>

#include "ff.h"

#include "playlist.h"
#include "fs_diskio.h"
#include "log.h"

typedef struct
{
    uint16_t count;
    int16_t  current;
    char     names[PLAYLIST_MAX_TRACKS][PLAYLIST_NAME_MAX];
} PLAYLIST_INFO_T;

static PLAYLIST_INFO_T s_playlist = {0};

/* f_readdir() reuses this; 255 + LFN bookkeeping lives on the caller's stack. */
static FILINFO s_file_info;

static bool is_wav_name(const char *name)
{
    size_t len = strlen(name);

    if (len < 5U)
    {
        return false;
    }

    return ((name[len - 4U] == '.') &&
            ((name[len - 3U] == 'w') || (name[len - 3U] == 'W')) &&
            ((name[len - 2U] == 'a') || (name[len - 2U] == 'A')) &&
            ((name[len - 1U] == 'v') || (name[len - 1U] == 'V')))
               ? true
               : false;
}

static void sort_names(void)
{
    uint16_t i;
    uint16_t j;
    char     key[PLAYLIST_NAME_MAX];

    for (i = 1U; i < s_playlist.count; i++)
    {
        memcpy(key, s_playlist.names[i], PLAYLIST_NAME_MAX);
        j = i;
        while ((j > 0U) && (strncmp(s_playlist.names[j - 1U], key, PLAYLIST_NAME_MAX) > 0))
        {
            memcpy(s_playlist.names[j], s_playlist.names[j - 1U], PLAYLIST_NAME_MAX);
            j--;
        }
        memcpy(s_playlist.names[j], key, PLAYLIST_NAME_MAX);
    }
}

bool playlist_scan(void)
{
    DIR      dir;
    FRESULT  rc;
    uint16_t found = 0U;
    size_t   name_len;
    int16_t  previous = s_playlist.current;

    s_playlist.count   = 0U;
    s_playlist.current = -1;

    fs_lock();

    rc = f_opendir(&dir, PLAYLIST_DIR);
    if (rc != FR_OK)
    {
        fs_unlock();
        PRINT_LOG("[PLAY] cannot open " PLAYLIST_DIR " (rc=%d)\r\n", (int32_t)rc);
        return false;
    }

    for (;;)
    {
        rc = f_readdir(&dir, &s_file_info);
        if ((rc != FR_OK) || (s_file_info.fname[0] == '\0'))
        {
            break;
        }
        if (s_file_info.fattrib & (AM_DIR | AM_HID | AM_SYS))
        {
            continue;
        }
        if (!is_wav_name(s_file_info.fname))
        {
            continue;
        }

        if (found >= PLAYLIST_MAX_TRACKS)
        {
            PRINT_LOG("[PLAY] track limit %u reached\r\n", (unsigned)PLAYLIST_MAX_TRACKS);
            break;
        }

        name_len = strlen(s_file_info.fname);
        if (name_len >= PLAYLIST_NAME_MAX)
        {
            name_len = PLAYLIST_NAME_MAX - 1U; /* keep the longest LFN representable */
        }
        memcpy(s_playlist.names[found], s_file_info.fname, name_len);
        s_playlist.names[found][name_len] = '\0';
        found++;
    }

    (void)f_closedir(&dir);
    fs_unlock();

    s_playlist.count = found;
    if (found == 0U)
    {
        PRINT_LOG("[PLAY] no .wav file in " PLAYLIST_DIR "\r\n");
        return false;
    }

    sort_names();

    /* Keep the same track selected across a rescan when it still exists. */
    s_playlist.current = 0;
    if ((previous >= 0) && (previous < (int16_t)found))
    {
        s_playlist.current = previous;
    }

    PRINT_LOG("[PLAY] %u track(s) in " PLAYLIST_DIR "\r\n", (unsigned)found);
    return true;
}

uint16_t playlist_count(void)
{
    return s_playlist.count;
}

int16_t playlist_current(void)
{
    return s_playlist.current;
}

const char *playlist_name(uint16_t index)
{
    if (index >= s_playlist.count)
    {
        return NULL;
    }
    return s_playlist.names[index];
}

bool playlist_step(int16_t delta)
{
    int16_t next;

    if (s_playlist.count == 0U)
    {
        return false;
    }

    next = (int16_t)(s_playlist.current + delta);
    if (next < 0)
    {
        next = (int16_t)(s_playlist.count - 1U);
    }
    if (next >= (int16_t)s_playlist.count)
    {
        next = 0;
    }

    s_playlist.current = next;
    return true;
}

bool playlist_select(uint16_t index)
{
    if ((s_playlist.count == 0U) || (index >= s_playlist.count))
    {
        return false;
    }
    s_playlist.current = (int16_t)index;
    return true;
}

bool playlist_path(uint16_t index, char *out, uint32_t out_size)
{
    int32_t written;

    if ((out == NULL) || (out_size == 0U) || (index >= s_playlist.count))
    {
        return false;
    }

    written = (int32_t)snprintf(out, out_size, "%s/%s", PLAYLIST_DIR, s_playlist.names[index]);
    return ((written > 0) && ((uint32_t)written < out_size)) ? true : false;
}
