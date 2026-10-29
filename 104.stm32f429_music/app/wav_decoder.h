/**
 ******************************************************************************
 * @file    wav_decoder.h
 * @brief   Minimal RIFF/WAVE parser and PCM reader (16-bit and 24-bit).
 *
 *  Ported from the ALIENTEK 音乐播放 experiment (AUDIOCODEC/wav/wavplay.c), but
 *  the header walk was rewritten: the reference hard-coded the offsets of the
 *  optional FACT/LIST chunks, which breaks on files that carry an ID3 or a
 *  metadata LIST.  Here every chunk is walked with its own size field.
 ******************************************************************************
 */
#ifndef WAV_DECODER_H
#define WAV_DECODER_H

#include <stdbool.h>
#include <stdint.h>

#include "ff.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint16_t audio_format;    /* 1 = linear PCM (the only accepted format)  */
    uint16_t channels;        /* 1 = mono, 2 = stereo                       */
    uint32_t sample_rate;     /* Hz                                          */
    uint32_t byte_rate;       /* bytes per second                            */
    uint16_t block_align;     /* bytes per sample frame                      */
    uint16_t bits_per_sample; /* 16 or 24                                    */
    uint32_t data_offset;     /* file offset of the first PCM byte           */
    uint32_t data_size;       /* PCM payload size in bytes                   */
    uint32_t total_seconds;   /* data_size / byte_rate                       */
} WAV_INFO_T;

/**
 * @brief  Parse the RIFF/WAVE header of an already-open file.
 *
 * @param  fp    open file (position is not restored on failure)
 * @param  info  receives the parsed description
 *
 * @retval true   linear PCM with a usable 'data' chunk
 * @retval false  not a WAV, unsupported encoding, or truncated header
 */
bool wav_parse_header(FIL *fp, WAV_INFO_T *info);

/**
 * @brief  Read the next PCM bytes and convert them to the SAI slot layout.
 *
 *         The SAI is a stereo master: every frame carries two slots, and the
 *         DMA advances one slot per transfer.  A mono source therefore HAS to be
 *         duplicated into both slots, otherwise the frame rate advances the
 *         stream by two samples per frame and the file plays at double speed.
 *
 *         16-bit: one 16-bit sample per slot (mono duplicated to L+R).
 *         24-bit: one 32-bit word per slot, sample left-aligned (bits 31..8),
 *                 because the SAI transmits a 24-bit payload inside a 32-bit
 *                 slot, MSB first.
 *
 *         A short read is zero-padded, so the DMA always gets a full buffer and
 *         the caller detects end-of-file from the returned count.
 *
 * @param  fp        open file positioned in the data chunk
 * @param  dst       destination buffer (4-byte aligned)
 * @param  bytes     buffer size in bytes
 * @param  bps       bits per sample from WAV_INFO_T
 * @param  channels  1 = mono (duplicated), 2 = stereo (verbatim)
 *
 * @retval number of bytes actually produced (equals `bytes` until EOF)
 */
uint32_t wav_read_pcm(FIL *fp, uint8_t *dst, uint32_t bytes, uint16_t bps, uint16_t channels);

#ifdef __cplusplus
}
#endif

#endif /* WAV_DECODER_H */
