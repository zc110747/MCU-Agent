/**
 ******************************************************************************
 * @file    mp3_decoder.h
 * @brief   MP3 (MPEG-1/2/2.5 Layer III) streaming decoder built on minimp3.
 *
 *  Output is always 16-bit stereo, interleaved, little endian - the same
 *  format the WAV path produces, so the SAI never has to be reconfigured for a
 *  different sample width.
 *
 *  The file is never touched directly: a small MP3_IO_T vtable supplies the
 *  bytes, which keeps this module free of FatFs and lets the exact same code
 *  be exercised by a host-side test harness.
 ******************************************************************************
 */
#ifndef MP3_DECODER_H
#define MP3_DECODER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Stream input hooks.  `read` returns the number of bytes stored in dst,
 *  0 meaning end of file. */
typedef struct
{
    void *handle;
    uint32_t (*read)(void *handle, uint8_t *dst, uint32_t len);
    bool (*seek)(void *handle, uint32_t offset); /* absolute, in bytes */
    uint32_t size;                               /* whole file in bytes */
} MP3_IO_T;

/** Stream description, valid after mp3_open() succeeded. */
typedef struct
{
    uint16_t sample_rate;
    uint16_t channels;      /* 1 or 2, as stored in the stream */
    uint16_t bitrate_kbps;  /* first frame; exact only for CBR */
    uint32_t total_seconds; /* from the Xing/Info frame count when present */
} MP3_INFO_T;

/**
 * @brief  Locate the first frame, read its header and arm the decoder.
 * @retval true when the stream contains at least one decodable frame.
 */
bool mp3_open(const MP3_IO_T *io, MP3_INFO_T *info);

/**
 * @brief  Produce up to `bytes` bytes of 16-bit stereo PCM.
 * @retval bytes produced; less than `bytes` only at end of stream, and the
 *         remainder is then zero padded so a DMA half buffer is never partly
 *         stale audio.
 */
uint32_t mp3_read(uint8_t *dst, uint32_t bytes);

/** Seconds of audio decoded so far (wall-clock independent). */
uint32_t mp3_position_seconds(void);

/** Time spent inside the decoder so far, in milliseconds. */
uint32_t mp3_decode_ms(void);

/** Release the decoder (clears the static state). */
void mp3_close(void);

#ifdef __cplusplus
}
#endif

#endif /* MP3_DECODER_H */
