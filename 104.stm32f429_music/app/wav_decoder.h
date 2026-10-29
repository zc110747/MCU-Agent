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
 * @brief  Read the next PCM bytes and normalise them to 16-bit stereo.
 *
 *         Output format is always 2 x 16 bit little endian per audio frame,
 *         which is what the SAI/WM8978 chain is configured for:
 *
 *         - 16-bit stereo : copied verbatim
 *         - 16-bit mono   : every sample duplicated into both slots
 *         - 24-bit stereo : top 16 bits of each sample
 *         - 24-bit mono   : top 16 bits, duplicated into both slots
 *
 *         Duplication is mandatory, not cosmetic: the SAI is a stereo master
 *         and advances one slot per DMA transfer, so a mono stream fed verbatim
 *         would play at double speed.
 *
 *         A short read is zero-padded, so the DMA always gets a full buffer and
 *         the caller detects end-of-file from the returned count.
 *
 * @param  fp     open file positioned in the data chunk
 * @param  dst    destination buffer (4-byte aligned)
 * @param  bytes  buffer size in bytes (multiple of 4)
 * @param  info   parsed header of the same file
 *
 * @retval number of bytes actually produced (equals `bytes` until EOF)
 */
uint32_t wav_read_pcm16(FIL *fp, uint8_t *dst, uint32_t bytes, const WAV_INFO_T *info);

#ifdef __cplusplus
}
#endif

#endif /* WAV_DECODER_H */
