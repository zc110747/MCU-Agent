/**
 ******************************************************************************
 * @file    track_decoder.c
 * @brief   Format dispatch + FatFs glue for the unified PCM source.
 *
 *  Locking: FatFs is not reentrant, so every f_read()/f_lseek() taken here
 *  holds fs_lock().  Callers must therefore NOT hold the lock when they call
 *  into this module (the mutex is not recursive).
 ******************************************************************************
 */
#include <string.h>

#include "track_decoder.h"

#include "fs_diskio.h"
#include "log.h"
#include "mp3_decoder.h"
#include "wav_decoder.h"

typedef struct
{
    TRACK_KIND_T kind;
    FIL         *fp;
    WAV_INFO_T   wav;
    MP3_INFO_T   mp3;
} TRACK_STATE_T;

static TRACK_STATE_T s_track = {0};

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */
static bool has_extension(const char *path, const char *ext)
{
    size_t path_len;
    size_t ext_len;
    size_t i;

    if ((path == NULL) || (ext == NULL))
    {
        return false;
    }

    path_len = strlen(path);
    ext_len  = strlen(ext);
    if (path_len <= ext_len)
    {
        return false;
    }
    if (path[path_len - ext_len - 1U] != '.')
    {
        return false;
    }

    for (i = 0U; i < ext_len; i++)
    {
        char a = path[path_len - ext_len + i];
        char b = ext[i];

        if ((a >= 'A') && (a <= 'Z'))
        {
            a = (char)(a - 'A' + 'a');
        }
        if (a != b)
        {
            return false;
        }
    }
    return true;
}

static uint32_t mp3_io_read(void *handle, uint8_t *dst, uint32_t len)
{
    FIL *fp = (FIL *)handle;
    UINT br = 0U;

    fs_lock();
    if (f_read(fp, dst, (UINT)len, &br) != FR_OK)
    {
        br = 0U;
    }
    fs_unlock();
    return (uint32_t)br;
}

static bool mp3_io_seek(void *handle, uint32_t offset)
{
    FIL    *fp = (FIL *)handle;
    FRESULT rc;

    fs_lock();
    rc = f_lseek(fp, (FSIZE_t)offset);
    fs_unlock();
    return (rc == FR_OK) ? true : false;
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */
bool track_decoder_open(FIL *fp, const char *path, TRACK_INFO_T *info)
{
    bool ok;

    if ((fp == NULL) || (path == NULL) || (info == NULL))
    {
        return false;
    }

    memset(&s_track, 0, sizeof(s_track));
    memset(info, 0, sizeof(*info));
    s_track.fp = fp;

    if (has_extension(path, "mp3"))
    {
        MP3_IO_T io;

        io.handle = fp;
        io.read   = mp3_io_read;
        io.seek   = mp3_io_seek;
        io.size   = (uint32_t)f_size(fp);

        if (!mp3_open(&io, &s_track.mp3))
        {
            return false;
        }

        s_track.kind          = TRACK_KIND_MP3;
        info->kind            = TRACK_KIND_MP3;
        info->sample_rate     = s_track.mp3.sample_rate;
        info->channels        = s_track.mp3.channels;
        info->bits_per_sample = 16U;
        info->total_seconds   = s_track.mp3.total_seconds;
        return true;
    }

    if (!has_extension(path, "wav"))
    {
        PRINT_LOG("[PLAY] unsupported file type: %s\r\n", path);
        return false;
    }

    fs_lock();
    ok = wav_parse_header(fp, &s_track.wav);
    if (ok)
    {
        ok = (f_lseek(fp, s_track.wav.data_offset) == FR_OK) ? true : false;
    }
    fs_unlock();
    if (!ok)
    {
        return false;
    }

    s_track.kind          = TRACK_KIND_WAV;
    info->kind            = TRACK_KIND_WAV;
    info->sample_rate     = (uint16_t)s_track.wav.sample_rate;
    info->channels        = s_track.wav.channels;
    info->bits_per_sample = s_track.wav.bits_per_sample;
    info->total_seconds   = s_track.wav.total_seconds;
    return true;
}

uint32_t track_decoder_read(uint8_t *dst, uint32_t bytes)
{
    uint32_t produced;

    if ((dst == NULL) || (bytes == 0U))
    {
        return 0U;
    }

    if (s_track.kind == TRACK_KIND_MP3)
    {
        return mp3_read(dst, bytes);
    }

    if (s_track.kind == TRACK_KIND_WAV)
    {
        fs_lock();
        produced = wav_read_pcm16(s_track.fp, dst, bytes, &s_track.wav);
        fs_unlock();
        return produced;
    }

    memset(dst, 0, bytes);
    return 0U;
}

uint32_t track_decoder_position_seconds(void)
{
    if (s_track.kind == TRACK_KIND_MP3)
    {
        return mp3_position_seconds();
    }

    if (s_track.kind == TRACK_KIND_WAV)
    {
        uint32_t played;

        if (s_track.wav.byte_rate == 0U)
        {
            return 0U;
        }
        played = (uint32_t)s_track.fp->fptr;
        played = (played > s_track.wav.data_offset) ? (played - s_track.wav.data_offset) : 0U;
        played = played / s_track.wav.byte_rate;
        return (played > s_track.wav.total_seconds) ? s_track.wav.total_seconds : played;
    }

    return 0U;
}

uint32_t track_decoder_decode_ms(void)
{
    return (s_track.kind == TRACK_KIND_MP3) ? mp3_decode_ms() : 0U;
}

uint32_t track_decoder_output_rate(void)
{
    /* 16-bit stereo: 4 bytes per frame. */
    uint32_t rate = 0U;

    if (s_track.kind == TRACK_KIND_MP3)
    {
        rate = s_track.mp3.sample_rate;
    }
    else if (s_track.kind == TRACK_KIND_WAV)
    {
        rate = s_track.wav.sample_rate;
    }
    return rate * 4U;
}

void track_decoder_close(void)
{
    if (s_track.kind == TRACK_KIND_MP3)
    {
        mp3_close();
    }
    memset(&s_track, 0, sizeof(s_track));
}
