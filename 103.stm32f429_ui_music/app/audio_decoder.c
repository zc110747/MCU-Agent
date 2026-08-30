/**
  ******************************************************************************
  * @file    app/audio_decoder.c
  * @brief   Format-agnostic PCM decoder: WAV (PCM 8/16 mono/stereo) and
  *         MP3 (minimp3 frame API).  All output is normalised to 16-bit signed
  *         stereo, interleaved L,R -- exactly what the SAI DMA expects.
  *
  *  Design notes
  *  ------------
  *  - WAV: the whole container is parsed up front; read() just walks the data
  *    chunk and widens/duplicates to stereo int16.  Seek is a plain byte lseek.
  *  - MP3: streamed through minimp3's frame API.  mp3_buf (SDRAM pointer) is an
  *    input staging buffer; read() appends file bytes until a full MPEG frame is available,
  *    then decodes and advances by info.frame_bytes.  Garbage between ID3 tags
  *    and the first frame is skipped by a sync-word rescan.  Duration is
  *    estimated from the (CBR) bitrate of the first frame -- VBR files get an
  *    approximate bar, flagged in the README.
  ******************************************************************************
  */
#include "audio_decoder.h"
#include "minimp3.h"
#include "log.h"
#include "FreeRTOS.h"

#include <string.h>

/* One decoded frame's worth of int16 (1152 * 2ch).  Allocated in SDRAM on the
 * first MP3 open and freed on close -- it must NOT live in internal SRAM. */
static int16_t *s_pcm = NULL;

/* Lightweight decode telemetry (read over SWD to localise a stuck prime):
 *   g_dec_reads     - audio_decoder_read() invocations
 *   g_dec_produced  - total stereo frames produced
 *   g_dec_fread     - f_read() calls inside the MP3 refill path
 *   g_dec_eof       - decode reached end-of-file (1 once set) */
volatile uint32_t g_dec_reads    = 0U;
volatile uint32_t g_dec_produced = 0U;
volatile uint32_t g_dec_fread    = 0U;
volatile uint32_t g_dec_eof      = 0U;
/* Decode-loop liveness (read over SWD).  g_dec_loop is the iteration count of
 * the LAST read_mp3_frames() call; g_dec_guard is non-zero if the watchdog
 * tripped because the loop could not make progress (pathological MP3 region).
 * A non-zero g_dec_guard means a single track contained a frame the decoder
 * loop could not advance past -> it was skipped so playback is not frozen. */
volatile uint32_t g_dec_loop     = 0U;
volatile uint32_t g_dec_guard    = 0U;

/* Case-insensitive extension match (newlib-nano has no strcasecmp). */
static int ext_is(const char *path, const char *ext)
{
    size_t pl, el;
    if (path == NULL || ext == NULL) return 0;
    pl = strlen(path);
    el = strlen(ext);
    if (pl < el) return 0;
    {
        size_t i;
        const char *p = path + (pl - el);
        for (i = 0U; i < el; i++)
        {
            char a = p[i];
            char b = ext[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) return 0;
        }
    }
    return 1;
}

/* WAV chunk scan: walk RIFF sub-chunks until "fmt "/"data" are found. */
static int wav_parse(dec_ctx_t *d)
{
    UINT br;
    uint8_t hdr[12];
    char tag[5];

    f_lseek(&d->fil, 0);
    if (f_read(&d->fil, hdr, 12U, &br) != FR_OK || br != 12U)
    {
        return -1;
    }
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0)
    {
        return -1;
    }

    /* Walk sub-chunks.  Each is: 4-char id, uint32 LE size, payload. */
    for (;;)
    {
        uint8_t ck[8];
        uint32_t sz;

        if (f_read(&d->fil, ck, 8U, &br) != FR_OK || br != 8U)
        {
            break; /* ran out before finding data -> malformed */
        }
        sz = (uint32_t)ck[4] | ((uint32_t)ck[5] << 8U) |
             ((uint32_t)ck[6] << 16U) | ((uint32_t)ck[7] << 24U);

        memcpy(tag, ck, 4);
        tag[4] = '\0';

        if (memcmp(ck, "fmt ", 4) == 0)
        {
            uint8_t fmt[16];
            uint16_t fmt_tag, nch, bits;
            uint32_t sr;

            if (f_read(&d->fil, fmt, 16U, &br) != FR_OK || br != 16U)
            {
                return -1;
            }
            fmt_tag = (uint16_t)fmt[0] | ((uint16_t)fmt[1] << 8U);
            nch     = (uint16_t)fmt[2] | ((uint16_t)fmt[3] << 8U);
            sr      = (uint32_t)fmt[4] | ((uint32_t)fmt[5] << 8U) |
                      ((uint32_t)fmt[6] << 16U) | ((uint32_t)fmt[7] << 24U);
            bits    = (uint16_t)fmt[14] | ((uint16_t)fmt[15] << 8U);

            if (fmt_tag != 1U) /* PCM only */
            {
                PRINT_LOG("[DEC ] WAV fmt %u not PCM\r\n", (unsigned)fmt_tag);
                return -1;
            }
            d->channels   = (uint8_t)(nch ? nch : 1U);
            d->sample_rate = sr;
            d->bits       = (uint8_t)bits;
            d->data_start = (uint32_t)f_tell(&d->fil); /* after fmt payload */
            d->data_bytes = sz >= 16U ? (sz - 16U) : 0U;

            /* Skip any fmt padding so the next read hits the next chunk. */
            if (sz > 16U)
            {
                f_lseek(&d->fil, d->data_start + d->data_bytes);
            }
        }
        else if (memcmp(ck, "data", 4) == 0)
        {
            d->data_start = (uint32_t)f_tell(&d->fil);
            d->data_bytes = sz;
            d->wav_pos    = 0U;
            break;
        }
        else
        {
            /* Unknown chunk: skip its payload (chunks are word-aligned). */
            uint32_t pad = (sz + 1U) & ~1U;
            f_lseek(&d->fil, f_tell(&d->fil) + pad);
        }
    }

    if (d->data_start == 0U || d->sample_rate == 0U)
    {
        return -1;
    }

    uint32_t bytes_per_sec = d->sample_rate * (uint32_t)d->channels *
                             ((uint32_t)d->bits / 8U);
    d->duration_ms = (bytes_per_sec != 0U)
                     ? (d->data_bytes * 1000U) / bytes_per_sec : 0U;
    return 0;
}

/* Find the next MPEG frame sync (0xFF Ex) in the staging buffer from mp3_off.
 * On success sets mp3_off to the sync position and returns 1; otherwise
 * compacts any trailing bytes to the front and returns 0. */
static int mp3_resync(dec_ctx_t *d)
{
    uint32_t i;

    for (i = d->mp3_off; i + 1U < d->mp3_valid; i++)
    {
        if (d->mp3_buf[i] == 0xFFU && (d->mp3_buf[i + 1U] & 0xE0U) == 0xE0U)
        {
            d->mp3_off = i;
            return 1;
        }
    }
    /* No sync in what we have: keep the trailing 1 byte (it might be 0xFF of a
     * frame split across reads) and read more on the next iteration. */
    if (d->mp3_off > 0U)
    {
        uint32_t keep = d->mp3_valid - d->mp3_off;
        if (keep > 0U)
        {
            memmove(d->mp3_buf, d->mp3_buf + d->mp3_off, keep);
        }
        d->mp3_valid = keep;
        d->mp3_off   = 0U;
    }
    return 0;
}

/* Cheap MPEG-1/2/2.5 Layer III header parse: extract sample rate, channel
 * count and bitrate WITHOUT running the full decoder (which would place a
 * ~16 KB mp3dec_scratch_t on the caller's stack).  Returns 0 on success.
 *
 * This lets audio_decoder_open() learn the format cheaply so that OPENING an
 * MP3 never triggers the first mp3dec_decode_frame() on the UI task's stack --
 * the actual PCM decode is deferred to the refill task. */
static int mp3_parse_header(const uint8_t *p, uint32_t *sr,
                            uint8_t *ch, uint32_t *kbps)
{
    static const uint16_t s_sr[3][4] = {
        {44100, 48000, 32000, 0},   /* MPEG1    */
        {22050, 24000, 16000, 0},   /* MPEG2    */
        {11025, 12000,  8000, 0}    /* MPEG2.5  */
    };
    static const uint16_t s_br[2][16] = {
        {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0}, /* MPEG1     */
        {0,  8, 16, 24, 32, 40, 48, 56, 64,  80,  96, 112, 128, 144, 160, 0}  /* MPEG2/2.5 */
    };
    uint8_t ver, layer, sr_idx, br_idx, ch_mode;
    uint8_t sr_row, br_row;

    if (p == NULL || sr == NULL || ch == NULL || kbps == NULL) return -1;
    if (p[0] != 0xFFU || (p[1] & 0xE0U) != 0xE0U) return -1;

    ver    = (p[1] >> 3) & 0x03U;   /* 11=MPEG1, 10=MPEG2, 00=MPEG2.5 */
    layer  = (p[1] >> 1) & 0x03U;   /* 01=Layer III */
    sr_idx = (p[2] >> 2) & 0x03U;
    br_idx = (p[2] >> 4) & 0x0FU;
    ch_mode = (p[3] >> 6) & 0x03U;

    if (layer != 0x01U) return -1;  /* not Layer III */

    if      (ver == 0x03U) { sr_row = 0U; br_row = 0U; }
    else if (ver == 0x02U) { sr_row = 1U; br_row = 1U; }
    else if (ver == 0x00U) { sr_row = 2U; br_row = 1U; }
    else                   { return -1; }   /* reserved version */

    if (s_sr[sr_row][sr_idx] == 0U) return -1;   /* reserved sample rate */
    if (s_br[br_row][br_idx] == 0U) return -1;   /* reserved/forbidden bitrate */

    *sr   = s_sr[sr_row][sr_idx];
    *ch   = (ch_mode == 0x03U) ? 1U : 2U;
    *kbps = s_br[br_row][br_idx];
    return 0;
}

int audio_decoder_open(dec_ctx_t *d, const char *path)
{
    FRESULT fr;
    const char *dot;
    UINT br;

    if (d == NULL || path == NULL)
    {
        return -1;
    }
    (void)memset(d, 0, sizeof(*d));

    dot = strrchr(path, '.');
    if (dot != NULL && (ext_is(path, ".mp3") || ext_is(path, ".mp2")))
    {
        d->fmt = AUDIO_FMT_MP3;
    }
    else if (dot != NULL && ext_is(path, ".wav"))
    {
        d->fmt = AUDIO_FMT_WAV;
    }
    else
    {
        PRINT_LOG("[DEC ] unsupported extension: %s\r\n", path);
        return -1;
    }

    fr = f_open(&d->fil, path, FA_READ);
    if (fr != FR_OK)
    {
        PRINT_LOG("[DEC ] f_open %s failed (%d)\r\n", path, (int)fr);
        return -1;
    }
    d->file_size = (uint32_t)f_size(&d->fil);

    if (d->fmt == AUDIO_FMT_WAV)
    {
        if (wav_parse(d) != 0)
        {
            f_close(&d->fil);
            return -1;
        }
        d->opened = 1U;
        return 0;
    }

    /* ---- MP3: read the head, decode one frame to learn the format ----- */
    /* Large buffers live in SDRAM (pvPortMalloc) per the project rule:
     * mp3_buf (input staging) and s_pcm (one decoded frame) are both big
     * enough that keeping them in internal SRAM would starve the heap. */
    d->mp3 = (mp3dec_t *)pvPortMalloc(sizeof(mp3dec_t));
    d->mp3_buf = (uint8_t *)pvPortMalloc(MP3_IN_BUF);
    s_pcm = (int16_t *)pvPortMalloc((size_t)MINIMP3_MAX_SAMPLES_PER_FRAME *
                                    sizeof(int16_t));
    if ((d->mp3 == NULL) || (d->mp3_buf == NULL) || (s_pcm == NULL))
    {
        PRINT_LOG("[DEC ] MP3: SDRAM alloc failed (mp3=%p buf=%p pcm=%p)\r\n",
                  (void *)d->mp3, (void *)d->mp3_buf, (void *)s_pcm);
        f_close(&d->fil);
        vPortFree(d->mp3);     d->mp3 = NULL;
        vPortFree(d->mp3_buf); d->mp3_buf = NULL;
        vPortFree(s_pcm);      s_pcm = NULL;
        return -1;
    }
    (void)memset(d->mp3, 0, sizeof(mp3dec_t));

    br = 0;
    if (f_read(&d->fil, d->mp3_buf, MP3_IN_BUF, &br) != FR_OK)
    {
        f_close(&d->fil);
        vPortFree(d->mp3);     d->mp3 = NULL;
        vPortFree(d->mp3_buf); d->mp3_buf = NULL;
        vPortFree(s_pcm);      s_pcm = NULL;
        return -1;
    }
    d->mp3_valid = (uint32_t)br;
    d->mp3_off   = 0U;
    d->mp3_consumed = 0U;

    mp3dec_init(d->mp3);
    if (mp3_resync(d) == 0)
    {
        PRINT_LOG("[DEC ] MP3: no frame sync found\r\n");
        f_close(&d->fil);
        vPortFree(d->mp3);     d->mp3 = NULL;
        vPortFree(d->mp3_buf); d->mp3_buf = NULL;
        vPortFree(s_pcm);      s_pcm = NULL;
        return -1;
    }

    {
        /* Learn the format from the first frame header only -- NO full decode,
         * so the ~16 KB mp3dec_scratch_t never lands on the caller (UI) stack.
         * The first PCM frame is left at mp3_off for the first read(), run
         * later from the refill task. */
        uint32_t sr = 0U;
        uint8_t  ch = 2U;
        uint32_t kbps = 0U;

        if ((d->mp3_valid - d->mp3_off) < 4U)
        {
            PRINT_LOG("[DEC ] MP3: first frame header truncated\r\n");
            f_close(&d->fil);
            vPortFree(d->mp3);     d->mp3 = NULL;
            vPortFree(d->mp3_buf); d->mp3_buf = NULL;
            vPortFree(s_pcm);      s_pcm = NULL;
            return -1;
        }
        if (mp3_parse_header(d->mp3_buf + d->mp3_off, &sr, &ch, &kbps) != 0)
        {
            PRINT_LOG("[DEC ] MP3: bad first-frame header\r\n");
            f_close(&d->fil);
            vPortFree(d->mp3);     d->mp3 = NULL;
            vPortFree(d->mp3_buf); d->mp3_buf = NULL;
            vPortFree(s_pcm);      s_pcm = NULL;
            return -1;
        }
        d->channels         = ch;
        d->sample_rate      = sr;
        d->bits             = 16U;
        d->mp3_bitrate_kbps = kbps;
        /* mp3_off stays at the frame start -> first read() decodes it. */

        if (kbps != 0U)
        {
            d->duration_ms = (d->file_size * 8000U) / kbps;
        }
    }

    d->opened = 1U;
    PRINT_LOG("[DEC ] MP3 open %s (%lu Hz, %u ch, %u kbps)\r\n",
              path, (unsigned long)d->sample_rate, (unsigned)d->channels,
              (unsigned)d->mp3_bitrate_kbps);
    return 0;
}

void audio_decoder_close(dec_ctx_t *d)
{
    if (d == NULL || d->opened == 0U)
    {
        return;
    }
    f_close(&d->fil);
    if (d->mp3 != NULL)
    {
        vPortFree(d->mp3);
        d->mp3 = NULL;
    }
    if (d->mp3_buf != NULL)
    {
        vPortFree(d->mp3_buf);
        d->mp3_buf = NULL;
    }
    if (s_pcm != NULL)
    {
        vPortFree(s_pcm);
        s_pcm = NULL;
    }
    (void)memset(d, 0, sizeof(*d));
}

/* Convert one source frame (already in s_pcm or read directly) is handled
 * inline; this pulls WAV source bytes and widens/duplicates to stereo. */
static uint32_t read_wav_frames(dec_ctx_t *d, int16_t *out, uint32_t want)
{
    uint32_t src_bpf = (uint32_t)d->channels * ((uint32_t)d->bits / 8U);
    uint32_t remain  = (d->data_bytes > d->wav_pos)
                       ? (d->data_bytes - d->wav_pos) / src_bpf : 0U;
    uint32_t n = (want < remain) ? want : remain;
    uint32_t i;
    UINT br;
    static uint8_t tmp[256];

    if (n == 0U)
    {
        d->eof = 1U;
        return 0U;
    }

    for (i = 0U; i < n; i++)
    {
        int16_t l, r;
        uint8_t s8[2];

        if (src_bpf <= sizeof(tmp))
        {
            if (f_read(&d->fil, tmp, src_bpf, &br) != FR_OK || br != src_bpf)
            {
                d->eof = 1U;
                return i;
            }
        }
        else
        {
            d->eof = 1U; /* defensive: src_bpf larger than tmp */
            return i;
        }

        if (d->bits == 8U)
        {
            s8[0] = tmp[0];
            s8[1] = (d->channels > 1U) ? tmp[1] : tmp[0];
            l = (int16_t)((int)(s8[0] - 128) << 8);
            r = (int16_t)((int)(s8[1] - 128) << 8);
        }
        else /* 16-bit signed LE */
        {
            l = (int16_t)((uint16_t)tmp[0] | ((uint16_t)tmp[1] << 8U));
            r = (d->channels > 1U)
                ? (int16_t)((uint16_t)tmp[2] | ((uint16_t)tmp[3] << 8U))
                : l;
        }

        out[2U * i]     = l;
        out[2U * i + 1U] = r;
    }

    d->wav_pos += n * src_bpf;
    if (d->wav_pos >= d->data_bytes)
    {
        d->eof = 1U;
    }
    {
        uint32_t bps = d->sample_rate * (uint32_t)d->channels *
                       ((uint32_t)d->bits / 8U);
        d->position_ms = (bps != 0U) ? (d->wav_pos * 1000U) / bps : 0U;
    }
    return n;
}

static uint32_t read_mp3_frames(dec_ctx_t *d, int16_t *out, uint32_t want)
{
    uint32_t produced = 0U;
    uint32_t guard    = 0U;

    while (produced < want)
    {
        guard++;
        g_dec_loop = guard;
        /* Watchdog: a single read() must always terminate.  A legitimate
         * half-buffer needs at most a few hundred loop iterations; if we ever
         * spin past 500000 it means the decoder loop is stuck on a pathological
         * frame/byte pattern.  Break out (the caller flags PLAYING via g0/g1)
         * so the prime never freezes the whole board. */
        if (guard > 500000U)
        {
            g_dec_guard = guard;
            break;
        }
        int avail;
        int samples;
        mp3dec_frame_info_t info;
        uint32_t frames;
        uint32_t j;

        /* 1. Staging buffer exhausted: shift any trailing bytes to the front
         *    and top up from the file (or stop at EOF). */
        if (d->mp3_off >= d->mp3_valid)
        {
            UINT br = 0U;
            if (d->mp3_off > 0U)
            {
                uint32_t keep = d->mp3_valid - d->mp3_off;
                if (keep > 0U)
                {
                    memmove(d->mp3_buf, d->mp3_buf + d->mp3_off, keep);
                }
                d->mp3_valid = keep;
                d->mp3_off   = 0U;
            }
            if (d->eof != 0U)
            {
                break;
            }
            g_dec_fread++;
            if (f_read(&d->fil, d->mp3_buf + d->mp3_valid,
                       MP3_IN_BUF - d->mp3_valid, &br) != FR_OK)
            {
                d->eof = 1U;
                break;
            }
            d->mp3_valid += (uint32_t)br;
            if (br == 0U)
            {
                d->eof = 1U;
            }
        }

        /* 2. Try to decode one frame. */
        avail  = (int)(d->mp3_valid - d->mp3_off);
        samples = mp3dec_decode_frame(d->mp3, d->mp3_buf + d->mp3_off,
                                      avail, s_pcm, &info);

        if (samples > 0)
        {
            d->mp3_off     += (uint32_t)info.frame_bytes;
            d->mp3_consumed += (uint32_t)info.frame_bytes;

            frames = (uint32_t)samples / (uint32_t)d->channels;
            for (j = 0U; j < frames && produced < want; j++)
            {
                int16_t l = s_pcm[j * d->channels + 0U];
                int16_t r = (d->channels > 1U)
                            ? s_pcm[j * d->channels + 1U] : l;
                out[2U * produced]     = l;
                out[2U * produced + 1U] = r;
                produced++;
            }
            continue;
        }

        if (info.frame_bytes > 0)
        {
            /* Decoder skipped a non-audio frame; advance and continue. */
            d->mp3_off     += (uint32_t)info.frame_bytes;
            d->mp3_consumed += (uint32_t)info.frame_bytes;
            continue;
        }

        /* 3. No frame decoded.  Resync; every branch below makes forward
         *    progress (consumes bytes, reads more, or stops) so this loop can
         *    never spin forever on garbage. */
        if (mp3_resync(d) != 0)
        {
            /* A sync word exists but the frame here is incomplete: step one
             * byte past it so the next pass re-scans for the real sync. */
            d->mp3_off += 1U;
            if (d->mp3_off >= d->mp3_valid)
            {
                d->mp3_off = d->mp3_valid;   /* top will refill */
            }
            continue;
        }

        /* No sync anywhere in the buffer. */
        if (d->eof != 0U)
        {
            break;
        }
        /* Drop the unusable bytes and let the next pass read fresh data. */
        d->mp3_off   = 0U;
        d->mp3_valid = 0U;
    }

    if (d->mp3_bitrate_kbps != 0U)
    {
        d->position_ms = (d->mp3_consumed * 8000U) / d->mp3_bitrate_kbps;
    }
    return produced;
}

int audio_decoder_read(dec_ctx_t *d, int16_t *out,
                       uint32_t want_frames, uint32_t *got_frames)
{
    uint32_t n;

    if (d == NULL || d->opened == 0U || out == NULL || got_frames == NULL)
    {
        if (got_frames != NULL) { *got_frames = 0U; }
        return -1;
    }
    if (d->eof != 0U)
    {
        *got_frames = 0U;
        return 0;
    }

    if (d->fmt == AUDIO_FMT_WAV)
    {
        n = read_wav_frames(d, out, want_frames);
    }
    else
    {
        n = read_mp3_frames(d, out, want_frames);
    }

    g_dec_reads++;
    g_dec_produced += n;
    if (d->eof != 0U) { g_dec_eof = 1U; }

    *got_frames = n;
    return 0;
}

int audio_decoder_seek(dec_ctx_t *d, uint32_t percent)
{
    if (d == NULL || d->opened == 0U)
    {
        return -1;
    }
    if (percent > 100U) { percent = 100U; }

    if (d->fmt == AUDIO_FMT_WAV)
    {
        uint32_t src_bpf = (uint32_t)d->channels * ((uint32_t)d->bits / 8U);
        uint32_t target  = ((uint64_t)d->data_bytes * percent) / 100U;
        target -= (target % src_bpf); /* align to a frame */
        d->wav_pos = target;
        f_lseek(&d->fil, d->data_start + target);
        d->eof = (d->wav_pos >= d->data_bytes) ? 1U : 0U;
        return 0;
    }

    /* MP3: byte seek + decoder reset + resync on next read. */
    {
        uint32_t byte_off = ((uint64_t)d->file_size * percent) / 100U;
        f_lseek(&d->fil, byte_off);
        if (d->mp3 != NULL)
        {
            mp3dec_init(d->mp3);
        }
        d->mp3_off     = 0U;
        d->mp3_valid   = 0U;
        d->mp3_consumed = byte_off;
        d->mp3_resync  = 1U;
        d->eof = 0U;
        d->position_ms = (d->mp3_bitrate_kbps != 0U)
                         ? (byte_off * 8000U) / d->mp3_bitrate_kbps : 0U;
        return 0;
    }
}
