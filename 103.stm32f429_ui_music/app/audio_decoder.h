/**
  ******************************************************************************
  * @file    app/audio_decoder.h
  * @brief   Format-agnostic PCM decoder for the music player.
  *
  *  Supports .wav (PCM 8/16-bit, mono/stereo) and .mp3 (via vendored minimp3).
  *  All decoded output is normalised to 16-bit signed stereo, interleaved
  *  L,R (what the SAI DMA expects), regardless of the source format.
  ******************************************************************************
  */
#ifndef AUDIO_DECODER_H
#define AUDIO_DECODER_H

#include <stdint.h>
#include "ff.h"
#include "minimp3.h"   /* mp3dec_t typedef (anonymous struct) + API */

/* Input staging buffer for the MP3 frame decoder.  Large enough to always
 * hold at least one worst-case MPEG frame plus slack for the streaming
 * "append more bytes" logic in audio_decoder_read(). */
#ifndef MP3_IN_BUF
#define MP3_IN_BUF   4096U
#endif

typedef enum
{
    AUDIO_FMT_WAV = 0,
    AUDIO_FMT_MP3 = 1
} audio_fmt_t;

typedef struct
{
    FIL            fil;
    audio_fmt_t    fmt;
    uint8_t        opened;
    uint32_t       sample_rate;
    uint8_t        channels;     /* source channels (1 or 2)          */
    uint8_t        bits;        /* wav only: 8 or 16                 */
    uint32_t       file_size;
    uint32_t       duration_ms;
    uint32_t       position_ms;

    /* WAV specifics */
    uint32_t       data_start;
    uint32_t       data_bytes;
    uint32_t       wav_pos;      /* bytes consumed from the data chunk */

    /* MP3 specifics (minimp3 frame API) */
    mp3dec_t      *mp3;
    uint8_t       *mp3_buf;      /* input staging buffer, pvPortMalloc'd in SDRAM */
    uint32_t       mp3_valid;    /* valid bytes currently in mp3_buf     */
    uint32_t       mp3_off;      /* consumed offset within mp3_buf      */
    uint32_t       mp3_consumed; /* raw bytes consumed from the file    */
    uint32_t       mp3_bitrate_kbps;
    uint8_t        mp3_resync;   /* resync at next read (after a seek)  */

    uint8_t        eof;
} dec_ctx_t;

/**
  * @brief  Open a track and parse its header.
  * @retval 0 ok, -1 cannot open / unsupported format
  */
int  audio_decoder_open(dec_ctx_t *d, const char *path);

/** @brief  Close the track file. */
void audio_decoder_close(dec_ctx_t *d);

/**
  * @brief  Decode up to want_frames of 16-bit stereo PCM into out.
  * @param  out         caller buffer, must hold want_frames * 2 int16
  * @param  got_frames  actual frames produced (may be < want at EOF)
  * @retval 0 ok, -1 error
  */
int  audio_decoder_read(dec_ctx_t *d, int16_t *out,
                        uint32_t want_frames, uint32_t *got_frames);

/**
  * @brief  Seek to a percentage (0..100) of the track duration.
  */
int  audio_decoder_seek(dec_ctx_t *d, uint32_t percent);

#endif /* AUDIO_DECODER_H */
