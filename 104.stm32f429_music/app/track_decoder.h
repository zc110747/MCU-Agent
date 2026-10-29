/**
 ******************************************************************************
 * @file    track_decoder.h
 * @brief   One uniform PCM source for WAV and MP3 tracks.
 *
 *  Everything downstream of this module (SAI, DMA, progress bar) only ever
 *  sees 16-bit stereo little-endian PCM, so the player needs no per-format
 *  branches:
 *
 *      WAV 16-bit mono      -> sample duplicated into both slots
 *      WAV 16-bit stereo    -> pass through
 *      WAV 24-bit mono/stereo -> truncated to the top 16 bits
 *      MP3 (any rate/mode)  -> minimp3, mono duplicated into both slots
 *
 *  The 24-bit path deliberately truncates: the codec runs 16-bit I2S, so the
 *  extra 8 bits never reached the DAC anyway, and keeping one output format
 *  removes the whole "24-bit word in a 32-bit slot" special case.
 ******************************************************************************
 */
#ifndef TRACK_DECODER_H
#define TRACK_DECODER_H

#include <stdbool.h>
#include <stdint.h>

#include "ff.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TRACK_KIND_NONE = 0,
    TRACK_KIND_WAV,
    TRACK_KIND_MP3,
} TRACK_KIND_T;

typedef struct
{
    TRACK_KIND_T kind;
    uint16_t     sample_rate;
    uint16_t     channels;        /* source channels, 1 or 2 */
    uint16_t     bits_per_sample; /* source bit depth, 16 or 24 */
    uint32_t     total_seconds;
} TRACK_INFO_T;

/**
 * @brief  Open a track and prepare it for streaming.
 * @param  fp    open file (the decoder seeks within it, never closes it)
 * @param  path  file name or full path; the extension picks the decoder
 * @retval true when the file was recognised and its header parsed
 */
bool track_decoder_open(FIL *fp, const char *path, TRACK_INFO_T *info);

/**
 * @brief  Produce up to `bytes` bytes of 16-bit stereo PCM, zero padding the
 *         tail at end of stream.
 * @retval bytes produced; less than `bytes` only at end of file
 */
uint32_t track_decoder_read(uint8_t *dst, uint32_t bytes);

/** Playback position in seconds, derived from decoded data (not wall clock). */
uint32_t track_decoder_position_seconds(void);

/** Time the MP3 decoder spent in this track, in milliseconds (0 for WAV). */
uint32_t track_decoder_decode_ms(void);

/** Bytes per second of the 16-bit stereo output stream. */
uint32_t track_decoder_output_rate(void);

/** Drop any decoder state.  The caller still owns the FIL. */
void track_decoder_close(void);

#ifdef __cplusplus
}
#endif

#endif /* TRACK_DECODER_H */
