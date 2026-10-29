/**
 ******************************************************************************
 * @file    wav_decoder.c
 * @brief   RIFF/WAVE header walk + PCM streaming.
 *
 *  All multi-byte fields are read byte by byte: the header buffer is not
 *  guaranteed to be aligned at every chunk boundary, and unaligned 32-bit
 *  loads are undefined behaviour even though the Cortex-M4 tolerates most of
 *  them.
 ******************************************************************************
 */
#include <string.h>

#include "wav_decoder.h"
#include "log.h"

/* Enough for RIFF + fmt + any FACT/LIST/ID3 preamble a normal file carries. */
#define WAV_SCAN_BYTES 1024U
/* 24-bit input is read 3 bytes per channel and repacked to 2.  A stereo chunk
 * therefore needs 1.5 bytes of staging per output byte; 6 KB covers a 4 KB
 * output buffer. */
#define WAV_24BIT_CHUNK 6144U
/* Output is always one stereo frame of two 16-bit slots. */
#define WAV_FRAME_BYTES 4U

#define WAV_ID_RIFF 0x46464952U /* "RIFF" */
#define WAV_ID_WAVE 0x45564157U /* "WAVE" */
#define WAV_ID_FMT 0x20746D66U  /* "fmt " */
#define WAV_ID_DATA 0x61746164U /* "data" */

static uint8_t s_scan[WAV_SCAN_BYTES] __attribute__((aligned(4)));
static uint8_t s_pack24[WAV_24BIT_CHUNK] __attribute__((aligned(4)));

static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd_u32(const uint8_t *p)
{
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool wav_parse_header(FIL *fp, WAV_INFO_T *info)
{
    UINT     br = 0U;
    uint32_t offset;
    bool     have_fmt  = false;
    bool     have_data = false;

    if ((fp == NULL) || (info == NULL))
    {
        return false;
    }

    memset(info, 0, sizeof(WAV_INFO_T));

    if (f_lseek(fp, 0U) != FR_OK)
    {
        return false;
    }
    if (f_read(fp, s_scan, WAV_SCAN_BYTES, &br) != FR_OK)
    {
        return false;
    }
    if (br < 12U)
    {
        return false;
    }
    if ((rd_u32(&s_scan[0]) != WAV_ID_RIFF) || (rd_u32(&s_scan[8]) != WAV_ID_WAVE))
    {
        return false;
    }

    offset = 12U;
    while ((offset + 8U) <= br)
    {
        uint32_t chunk_id   = rd_u32(&s_scan[offset]);
        uint32_t chunk_size = rd_u32(&s_scan[offset + 4U]);
        uint32_t body       = offset + 8U;

        if (chunk_id == WAV_ID_FMT)
        {
            if ((body + 16U) > br)
            {
                return false;
            }
            info->audio_format    = rd_u16(&s_scan[body + 0U]);
            info->channels        = rd_u16(&s_scan[body + 2U]);
            info->sample_rate     = rd_u32(&s_scan[body + 4U]);
            info->byte_rate       = rd_u32(&s_scan[body + 8U]);
            info->block_align     = rd_u16(&s_scan[body + 12U]);
            info->bits_per_sample = rd_u16(&s_scan[body + 14U]);
            have_fmt              = true;
        }
        else if (chunk_id == WAV_ID_DATA)
        {
            info->data_offset = body;
            info->data_size   = chunk_size;
            have_data         = true;
            break;
        }

        /* Chunks are word aligned: an odd size is followed by one pad byte. */
        offset = body + chunk_size + (chunk_size & 1U);
    }

    if (!have_fmt || !have_data)
    {
        PRINT_LOG("[WAV ] header incomplete (fmt=%u data=%u)\r\n",
                  (unsigned)have_fmt, (unsigned)have_data);
        return false;
    }
    if (info->audio_format != 1U)
    {
        PRINT_LOG("[WAV ] only linear PCM is supported (format=%u)\r\n",
                  (unsigned)info->audio_format);
        return false;
    }
    if ((info->bits_per_sample != 16U) && (info->bits_per_sample != 24U))
    {
        PRINT_LOG("[WAV ] unsupported bit depth %u\r\n", (unsigned)info->bits_per_sample);
        return false;
    }
    if ((info->byte_rate == 0U) || (info->data_size == 0U))
    {
        return false;
    }

    info->total_seconds = info->data_size / info->byte_rate;

    PRINT_LOG("[WAV ] %lu Hz %u ch %u bit, %lu s\r\n",
              (unsigned long)info->sample_rate,
              (unsigned)info->channels,
              (unsigned)info->bits_per_sample,
              (unsigned long)info->total_seconds);
    return true;
}

/**
 * @brief  Duplicate every 16-bit sample into both slots, in place.
 *
 *         The SAI runs as a stereo master and consumes two slots per frame, so
 *         a mono stream fed verbatim would advance by two source samples per
 *         frame - i.e. play at twice the intended rate.  Expanding in place
 *         from the end backwards never overwrites a sample that is still to be
 *         read (destination index 2i >= source index i).
 *
 * @param  dst      buffer holding `samples` packed 16-bit samples
 * @param  samples  number of mono samples present
 * @retval bytes written (2 slots per sample)
 */
static uint32_t expand_mono_to_slots(uint8_t *dst, uint32_t samples)
{
    uint16_t *hw = (uint16_t *)(void *)dst;
    uint32_t  i;

    for (i = samples; i > 0U; i--)
    {
        uint16_t v        = hw[i - 1U];
        hw[(i - 1U) * 2U] = v;
        hw[(i * 2U) - 1U] = v;
    }
    return samples * WAV_FRAME_BYTES;
}

/**
 * @brief  24-bit payload -> 16-bit stereo slots.
 *
 *         A 24-bit sample is stored little endian as 3 bytes; keeping the top
 *         16 bits means dropping the first byte.  The source lives in
 *         s_pack24, the destination in dst, so the two never overlap and the
 *         loop can run forwards.
 */
static uint32_t pack24_to_slots16(uint8_t *dst, const uint8_t *src, uint32_t samples,
                                  uint16_t channels)
{
    uint32_t i;

    for (i = 0U; i < samples; i++)
    {
        uint32_t frame = (channels == 1U) ? i : (i / 2U);
        uint32_t slot  = (channels == 1U) ? 0U : (i & 1U);
        uint32_t out   = (frame * 2U + slot) * 2U;

        dst[out]      = src[i * 3U + 1U];
        dst[out + 1U] = src[i * 3U + 2U];

        if (channels == 1U)
        {
            dst[out + 2U] = src[i * 3U + 1U];
            dst[out + 3U] = src[i * 3U + 2U];
        }
    }
    return samples * ((channels == 1U) ? 4U : 2U);
}

uint32_t wav_read_pcm16(FIL *fp, uint8_t *dst, uint32_t bytes, const WAV_INFO_T *info)
{
    UINT     br = 0U;
    uint32_t produced;

    if ((fp == NULL) || (dst == NULL) || (bytes == 0U) || (info == NULL))
    {
        return 0U;
    }

    bytes -= bytes % WAV_FRAME_BYTES;

    if (info->bits_per_sample == 24U)
    {
        /* frames = bytes / 4 slots; each frame needs 3 bytes per channel. */
        uint32_t frames = bytes / WAV_FRAME_BYTES;
        uint32_t raw    = frames * ((info->channels == 1U) ? 3U : 6U);
        uint32_t done;

        if (raw > WAV_24BIT_CHUNK)
        {
            raw    = WAV_24BIT_CHUNK - (WAV_24BIT_CHUNK % 3U);
            frames = raw / ((info->channels == 1U) ? 3U : 6U);
        }

        if (f_read(fp, s_pack24, raw, &br) != FR_OK)
        {
            br = 0U;
        }
        done     = br / ((info->channels == 1U) ? 3U : 6U);
        produced = pack24_to_slots16(dst, s_pack24, done, info->channels);
    }
    else if (info->channels == 1U)
    {
        /* Read the mono half of the target, then stretch it to both slots. */
        uint32_t mono_bytes = bytes / 2U;

        if (f_read(fp, dst, (UINT)mono_bytes, &br) != FR_OK)
        {
            br = 0U;
        }
        produced = expand_mono_to_slots(dst, br / 2U);
    }
    else
    {
        if (f_read(fp, dst, (UINT)bytes, &br) != FR_OK)
        {
            br = 0U;
        }
        produced = br - (br % WAV_FRAME_BYTES);
    }

    if (produced < bytes)
    {
        memset(dst + produced, 0, bytes - produced);
    }
    return produced;
}
