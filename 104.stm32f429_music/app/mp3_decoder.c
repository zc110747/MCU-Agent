/**
 ******************************************************************************
 * @file    mp3_decoder.c
 * @brief   MP3 streaming decoder: ID3v2 skip, frame header scan, minimp3 loop.
 *
 *  Buffering model
 *  ---------------
 *  A single 5 KB input window holds whole frames.  Frames are framed here from
 *  their own header, and minimp3 is always handed exactly one complete frame:
 *  given an incomplete first frame it resets its internal state, which also
 *  discards the Layer III bit reservoir and silently kills the next frames
 *  (measured: 87 of 385 frames lost on a 128 kbps file when the window
 *  happened to end mid-frame).
 *
 *  The decoder emits one frame at a time into a staging buffer
 *  (1152 samples x 2 channels), from which mp3_read() copies as much as the
 *  caller asked for.  That decouples the DMA half-buffer size (4096 B) from
 *  the MP3 frame size.
 ******************************************************************************
 */
#include <string.h>

#include "mp3_decoder.h"

#include "minimp3.h"

#include "log.h"

#ifndef MP3_HOST_TEST
#include "stm32f4xx_hal.h"
#define MP3_NOW_MS() HAL_GetTick()
#else
#define MP3_NOW_MS() 0U
#endif

/* 4 KB of input plus enough slack to assemble a frame that straddles the
 * refill boundary. */
#define MP3_IN_BYTES (4096U + 1024U)

/* Bytes of PCM per audio frame: L + R, 16 bit each. */
#define MP3_OUT_FRAME_BYTES 4U

typedef struct
{
    uint16_t sample_rate;
    uint16_t bitrate_kbps;
    uint16_t samples_per_frame;
    uint8_t  channels;
    uint8_t  side_info_bytes;
    uint8_t  crc_bytes;
    bool     mpeg1;
    uint32_t frame_bytes;
} MP3_HEADER_T;

typedef struct
{
    MP3_IO_T io;
    mp3dec_t dec;
    int16_t  pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    uint8_t  in[MP3_IN_BYTES];

    uint32_t in_len;
    uint32_t pcm_pos;   /* in samples */
    uint32_t pcm_count; /* in samples, always an even (stereo) count */
    uint32_t samples_total;
    uint32_t decode_ms;
    uint16_t sample_rate;
    uint16_t channels;
    bool     eof;
} MP3_STATE_T;

static MP3_STATE_T s_mp3;

/* -------------------------------------------------------------------------- */
/* Header parsing                                                             */
/* -------------------------------------------------------------------------- */
/* Bitrate tables, kbit/s, indexed by the 4-bit bitrate field. */
static const uint16_t s_bitrate_v1[16] = {
    0U, 32U, 40U, 48U, 56U, 64U, 80U, 96U, 112U, 128U, 160U, 192U, 224U, 256U, 320U, 0U};
static const uint16_t s_bitrate_v2[16] = {
    0U, 8U, 16U, 24U, 32U, 40U, 48U, 56U, 64U, 80U, 96U, 112U, 128U, 144U, 160U, 0U};

/* Sample rates, indexed by [MPEG version][sample-rate field]. */
static const uint16_t s_sample_rate[3][3] = {
    {44100U, 48000U, 32000U}, /* MPEG 1   */
    {22050U, 24000U, 16000U}, /* MPEG 2   */
    {11025U, 12000U, 8000U},  /* MPEG 2.5 */
};

static uint32_t rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | ((uint32_t)p[3]);
}

static bool mp3_parse_header(const uint8_t *p, MP3_HEADER_T *h)
{
    uint32_t version;
    uint32_t layer;
    uint32_t bitrate_idx;
    uint32_t rate_idx;
    uint32_t padding;
    uint32_t mode;

    if ((p[0] != 0xFFU) || ((p[1] & 0xE0U) != 0xE0U))
    {
        return false;
    }

    version = (uint32_t)((p[1] >> 3) & 0x03U);
    layer   = (uint32_t)((p[1] >> 1) & 0x03U);
    if ((version == 1U) || (layer != 1U))
    {
        return false; /* reserved version, or not Layer III */
    }

    bitrate_idx = (uint32_t)((p[2] >> 4) & 0x0FU);
    rate_idx    = (uint32_t)((p[2] >> 2) & 0x03U);
    if ((bitrate_idx == 0U) || (bitrate_idx == 15U) || (rate_idx == 3U))
    {
        return false;
    }

    padding = (uint32_t)((p[2] >> 1) & 0x01U);
    mode    = (uint32_t)((p[3] >> 6) & 0x03U);

    h->mpeg1             = (version == 3U);
    h->sample_rate       = s_sample_rate[h->mpeg1 ? 0U : ((version == 2U) ? 1U : 2U)][rate_idx];
    h->bitrate_kbps      = h->mpeg1 ? s_bitrate_v1[bitrate_idx] : s_bitrate_v2[bitrate_idx];
    h->channels          = (mode == 3U) ? 1U : 2U;
    h->samples_per_frame = h->mpeg1 ? 1152U : 576U;
    h->side_info_bytes =
        (uint8_t)(h->mpeg1 ? ((h->channels == 1U) ? 17U : 32U)
                           : ((h->channels == 1U) ? 9U : 17U));
    h->crc_bytes = (uint8_t)((p[1] & 0x01U) ? 0U : 2U);

    /* Layer III frame length = (samples/8) * bitrate / rate + padding. */
    h->frame_bytes =
        (((uint32_t)h->samples_per_frame / 8U) * (uint32_t)h->bitrate_kbps * 1000U) /
            h->sample_rate +
        padding;
    return h->frame_bytes != 0U;
}

/* -------------------------------------------------------------------------- */
/* File window                                                                */
/* -------------------------------------------------------------------------- */
static bool mp3_refill(void)
{
    uint32_t space;
    uint32_t got;

    if (s_mp3.eof)
    {
        return false;
    }

    space = (uint32_t)sizeof(s_mp3.in) - s_mp3.in_len;
    if (space == 0U)
    {
        /* Window full yet no frame was found in it: drop one byte and rescan.
         * Only reachable on corrupted input, and it guarantees forward
         * progress instead of spinning. */
        memmove(s_mp3.in, s_mp3.in + 1U, s_mp3.in_len - 1U);
        s_mp3.in_len--;
        space = 1U;
    }

    got = s_mp3.io.read(s_mp3.io.handle, s_mp3.in + s_mp3.in_len, space);
    if (got == 0U)
    {
        s_mp3.eof = true;
        return false;
    }

    s_mp3.in_len += got;
    return true;
}

static void mp3_consume(uint32_t bytes)
{
    if (bytes >= s_mp3.in_len)
    {
        s_mp3.in_len = 0U;
        return;
    }
    memmove(s_mp3.in, s_mp3.in + bytes, s_mp3.in_len - bytes);
    s_mp3.in_len -= bytes;
}

/* -------------------------------------------------------------------------- */
/* Decoding                                                                   */
/* -------------------------------------------------------------------------- */
/**
 * @brief  Make sure at least `bytes` bytes are buffered.
 * @retval false when the file ended before that much data arrived.
 */
static bool mp3_need(uint32_t bytes)
{
    while (s_mp3.in_len < bytes)
    {
        if (!mp3_refill())
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief  Decode exactly one frame into s_mp3.pcm[].
 *
 *         The frame is framed here, not inside minimp3, and the decoder is
 *         always handed a buffer that holds exactly one whole frame.  That is a
 *         hard requirement: when minimp3 is given a buffer whose first frame is
 *         incomplete it clears its whole state (`memset(dec, 0, ...)`), which
 *         also drops the Layer III bit reservoir.  Every following frame that
 *         references previous frames' main_data then fails to decode and is
 *         skipped silently - measured at ~23 % of the frames lost when the 5 KB
 *         window happened to end mid-frame, i.e. on every refill.
 *
 * @retval true when pcm[] holds a frame, false at end of stream.
 */
static bool mp3_next_frame(void)
{
    for (;;)
    {
        MP3_HEADER_T        hdr;
        mp3dec_frame_info_t info;
        int                 samples;
        uint32_t            started;

        if (!mp3_need(4U))
        {
            return false;
        }

        if (!mp3_parse_header(s_mp3.in, &hdr))
        {
            /* Lost sync (corrupt frame): walk forward one byte and retry. */
            mp3_consume(1U);
            continue;
        }

        if (!mp3_need(hdr.frame_bytes))
        {
            /* Truncated last frame - nothing more to play. */
            return false;
        }

        started = MP3_NOW_MS();
        samples = mp3dec_decode_frame(&s_mp3.dec, s_mp3.in, (int)hdr.frame_bytes,
                                      s_mp3.pcm, &info);
        s_mp3.decode_ms += MP3_NOW_MS() - started;

        mp3_consume(hdr.frame_bytes);

        if (samples <= 0)
        {
            /* The first frame(s) of a stream carry no audio of their own: their
             * main_data lives in frames that have not arrived yet. */
            continue;
        }

        s_mp3.sample_rate = (uint16_t)info.hz;
        s_mp3.channels    = (uint16_t)info.channels;
        s_mp3.samples_total += (uint32_t)samples;
        s_mp3.pcm_pos = 0U;

        if (info.channels == 1)
        {
            /* Duplicate into both slots, from the end backwards so no sample
             * is overwritten before it is read (2i >= i). */
            int i;

            for (i = samples - 1; i >= 0; i--)
            {
                s_mp3.pcm[i * 2]       = s_mp3.pcm[i];
                s_mp3.pcm[(i * 2) + 1] = s_mp3.pcm[i];
            }
        }

        s_mp3.pcm_count = (uint32_t)samples * 2U;
        return true;
    }
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */
bool mp3_open(const MP3_IO_T *io, MP3_INFO_T *info)
{
    MP3_HEADER_T hdr;
    uint32_t     start = 0U;
    uint32_t     got;
    uint32_t     off;
    uint32_t     total = 0U;
    bool         found = false;

    if ((io == NULL) || (io->read == NULL) || (io->seek == NULL) || (info == NULL))
    {
        return false;
    }

    memset(&s_mp3, 0, sizeof(s_mp3));
    memset(info, 0, sizeof(*info));
    s_mp3.io = *io;

    /* ---- ID3v2: 10 byte header + synchsafe size, optional footer ---- */
    (void)s_mp3.io.seek(s_mp3.io.handle, 0U);
    got = s_mp3.io.read(s_mp3.io.handle, s_mp3.in, 10U);
    if ((got == 10U) && (memcmp(s_mp3.in, "ID3", 3U) == 0))
    {
        uint32_t tag_size = ((uint32_t)(s_mp3.in[6] & 0x7FU) << 21) |
                            ((uint32_t)(s_mp3.in[7] & 0x7FU) << 14) |
                            ((uint32_t)(s_mp3.in[8] & 0x7FU) << 7) |
                            ((uint32_t)(s_mp3.in[9] & 0x7FU));

        start = 10U + tag_size;
        if ((s_mp3.in[5] & 0x10U) != 0U)
        {
            start += 10U; /* footer present */
        }
        if (start >= s_mp3.io.size)
        {
            start = 0U; /* bogus tag: fall back to scanning from the top */
        }
    }

    /* ---- locate and parse the first frame ---- */
    (void)s_mp3.io.seek(s_mp3.io.handle, start);
    s_mp3.in_len = s_mp3.io.read(s_mp3.io.handle, s_mp3.in, (uint32_t)sizeof(s_mp3.in));
    if (s_mp3.in_len < 4U)
    {
        PRINT_LOG("[MP3 ] file too short\r\n");
        return false;
    }

    for (off = 0U; (off + 4U) <= s_mp3.in_len; off++)
    {
        if (mp3_parse_header(&s_mp3.in[off], &hdr))
        {
            found = true;
            break;
        }
    }
    if (!found)
    {
        PRINT_LOG("[MP3 ] no frame header found\r\n");
        return false;
    }

    /* ---- duration: Xing/Info frame count, else a CBR estimate ---- */
    {
        uint32_t xing = off + 4U + hdr.crc_bytes + hdr.side_info_bytes;

        if (((xing + 12U) <= s_mp3.in_len) &&
            ((memcmp(&s_mp3.in[xing], "Xing", 4U) == 0) ||
             (memcmp(&s_mp3.in[xing], "Info", 4U) == 0)))
        {
            uint32_t flags = rd_be32(&s_mp3.in[xing + 4U]);

            if ((flags & 0x01U) != 0U)
            {
                uint32_t frames = rd_be32(&s_mp3.in[xing + 8U]);

                total = (uint32_t)(((uint64_t)frames * hdr.samples_per_frame) /
                                   hdr.sample_rate);
            }
        }

        if (total == 0U)
        {
            uint32_t data = (s_mp3.io.size > (start + off))
                                ? (s_mp3.io.size - (start + off))
                                : 0U;

            if (hdr.bitrate_kbps != 0U)
            {
                total = (uint32_t)(((uint64_t)data * 8U) /
                                   ((uint64_t)hdr.bitrate_kbps * 1000U));
            }
        }
    }

    /* ---- rewind to the first frame so decoding starts exactly there ---- */
    s_mp3.in_len = 0U;
    s_mp3.eof    = false;
    (void)s_mp3.io.seek(s_mp3.io.handle, start + off);

    info->sample_rate   = hdr.sample_rate;
    info->channels      = hdr.channels;
    info->bitrate_kbps  = hdr.bitrate_kbps;
    info->total_seconds = total;

    PRINT_LOG("[MP3 ] %u Hz %u ch %u kbps, %lu s\r\n",
              (unsigned)info->sample_rate, (unsigned)info->channels,
              (unsigned)info->bitrate_kbps, (unsigned long)info->total_seconds);
    return true;
}

uint32_t mp3_read(uint8_t *dst, uint32_t bytes)
{
    uint32_t out = 0U;

    if (dst == NULL)
    {
        return 0U;
    }

    while (out < bytes)
    {
        uint32_t avail;
        uint32_t want;
        uint32_t n;

        if (s_mp3.pcm_pos >= s_mp3.pcm_count)
        {
            if (!mp3_next_frame())
            {
                break;
            }
        }

        avail = (s_mp3.pcm_count - s_mp3.pcm_pos) * 2U; /* samples -> bytes */
        want  = bytes - out;
        n     = (avail < want) ? avail : want;
        n &= ~1U; /* never split a 16-bit sample */

        memcpy(dst + out, &s_mp3.pcm[s_mp3.pcm_pos], n);
        s_mp3.pcm_pos += n / 2U;
        out += n;
    }

    if (out < bytes)
    {
        memset(dst + out, 0, bytes - out);
    }
    return out;
}

uint32_t mp3_position_seconds(void)
{
    if (s_mp3.sample_rate == 0U)
    {
        return 0U;
    }
    return s_mp3.samples_total / s_mp3.sample_rate;
}

uint32_t mp3_decode_ms(void)
{
    return s_mp3.decode_ms;
}

void mp3_close(void)
{
    memset(&s_mp3, 0, sizeof(s_mp3));
}
