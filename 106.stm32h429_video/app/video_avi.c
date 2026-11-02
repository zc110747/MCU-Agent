/**
 ******************************************************************************
 * @file    video_avi.c
 * @brief   AVI (RIFF) header parsing for the MJPEG player.
 *
 *          The layout checks mirror the ALIENTEK 实验47 reference: every step
 *          offsets from the previous block's own size field, so extra chunks
 *          (JUNK, idx1, ...) do not confuse the walk.
 ******************************************************************************
 */
#include <string.h>

#include "video_avi.h"
#include "log.h"

/* File layout, little endian, naturally aligned: no packing attribute needed
 * on Cortex-M4 (every member sits on its own alignment boundary). */
typedef struct
{
    uint32_t riff_id;
    uint32_t file_size;
    uint32_t avi_id;
} avi_header_t;

typedef struct
{
    uint32_t list_id;
    uint32_t block_size;
    uint32_t list_type;
} list_header_t;

typedef struct
{
    uint32_t block_id;
    uint32_t block_size;
    uint32_t sec_per_frame;
    uint32_t max_bytes_per_sec;
    uint32_t padding_granularity;
    uint32_t flags;
    uint32_t total_frames;
    uint32_t init_frames;
    uint32_t streams;
    uint32_t ref_buf_size;
    uint32_t width;
    uint32_t height;
    uint32_t reserved[4];
} avih_header_t;

typedef struct
{
    uint32_t block_id;
    uint32_t block_size;
    uint32_t stream_type;
    uint32_t handler;
    uint32_t flags;
    uint16_t priority;
    uint16_t language;
    uint32_t init_frames;
    uint32_t scale;
    uint32_t rate;
    uint32_t start;
    uint32_t length;
    uint32_t ref_buf_size;
    uint32_t quality;
    uint32_t sample_size;
    struct
    {
        int16_t left;
        int16_t top;
        int16_t right;
        int16_t bottom;
    } frame;
} strh_header_t;

typedef struct
{
    uint32_t block_id;
    uint32_t block_size;
    uint32_t size;
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bit_count;
    uint32_t compression;
    uint32_t size_image;
    int32_t x_pels_per_meter;
    int32_t y_pels_per_meter;
    uint32_t clr_used;
    uint32_t clr_important;
} bmp_header_t;

typedef struct
{
    uint32_t block_id;
    uint32_t block_size;
    bmp_header_t bmi_header;
} strf_bmp_header_t;

typedef struct
{
    uint32_t block_id;
    uint32_t block_size;
    uint16_t format_tag;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t avg_bytes_per_sec;
    uint16_t block_align;
    uint16_t bits_per_sample;
} strf_wav_header_t;

/* Current file description. */
static avi_info_t s_avi = {0};

/* Stream tags per stream index: index 0/1 matches "00xx" and "01xx". */
static const char *const s_vids_flags[2] = { "00dc", "01dc" };
static const char *const s_auds_flags[2] = { "00wb", "01wb" };

const avi_info_t *avi_info(void)
{
    return &s_avi;
}

/** Offset of a strf chunk that follows the strh block at @p strl. */
static const uint8_t *avi_strf_after(const uint8_t *strl,
                                     const strh_header_t *strh)
{
    return strl + 12 + strh->block_size + 8;
}

/** Next list in the header, skipping the strl block at @p strl. */
static const uint8_t *avi_next_list(const uint8_t *strl,
                                    const list_header_t *list)
{
    return strl + list->block_size + 8;
}

/**
 * @brief  Walk the one or two strl lists (video and/or audio) of the header.
 *         Width/height, audio format and both stream tags come out of here.
 */
static avi_status_t avi_parse_streams(const uint8_t *strl,
                                      const list_header_t *list)
{
    const strh_header_t *strh;
    const uint8_t *next;

    strh = (const strh_header_t *)(strl + 12);
    if (strh->block_id != AVI_STRH_ID)
    {
        return AVI_STRH_ERR;
    }

    if (strh->stream_type == AVI_AUDS_STREAM)
    {
        /* Audio stream listed first: video is the second strl. */
        const strf_wav_header_t *wav;
        const strf_bmp_header_t *bmp;
        const list_header_t *second;

        s_avi.video_flag = s_vids_flags[1];
        s_avi.audio_flag = s_auds_flags[0];

        wav = (const strf_wav_header_t *)avi_strf_after(strl, strh);
        if (wav->block_id != AVI_STRF_ID)
        {
            return AVI_STRF_ERR;
        }
        s_avi.sample_rate = wav->sample_rate;
        s_avi.channels = wav->channels;
        s_avi.audio_type = wav->format_tag;

        next = avi_next_list(strl, list);
        second = (const list_header_t *)next;
        if (second->list_id != AVI_LIST_ID)
        {
            return AVI_LIST_ERR;
        }
        if (second->list_type != AVI_STRL_ID)
        {
            return AVI_STRL_ERR;
        }

        strh = (const strh_header_t *)(next + 12);
        if (strh->block_id != AVI_STRH_ID)
        {
            return AVI_STRH_ERR;
        }
        if (strh->stream_type != AVI_VIDS_STREAM)
        {
            return AVI_FORMAT_ERR;
        }

        bmp = (const strf_bmp_header_t *)avi_strf_after(next, strh);
        if (bmp->block_id != AVI_STRF_ID)
        {
            return AVI_STRF_ERR;
        }
        if (bmp->bmi_header.compression != AVI_FORMAT_MJPG)
        {
            return AVI_FORMAT_ERR;
        }
        s_avi.width = (uint32_t)bmp->bmi_header.width;
        s_avi.height = (uint32_t)bmp->bmi_header.height;
        return AVI_OK;
    }

    if (strh->stream_type != AVI_VIDS_STREAM)
    {
        return AVI_FORMAT_ERR;
    }

    /* Video stream listed first: audio is optional. */
    {
        const strf_bmp_header_t *bmp;
        const list_header_t *second;

        if (strh->handler != AVI_FORMAT_MJPG)
        {
            return AVI_FORMAT_ERR;
        }
        s_avi.video_flag = s_vids_flags[0];
        s_avi.audio_flag = s_auds_flags[1];

        bmp = (const strf_bmp_header_t *)avi_strf_after(strl, strh);
        if (bmp->block_id != AVI_STRF_ID)
        {
            return AVI_STRF_ERR;
        }
        s_avi.width = (uint32_t)bmp->bmi_header.width;
        s_avi.height = (uint32_t)bmp->bmi_header.height;

        next = avi_next_list(strl, list);
        second = (const list_header_t *)next;
        if (second->list_id != AVI_LIST_ID)
        {
            /* Video-only file: no audio stream at all. */
            s_avi.sample_rate = 0;
            s_avi.channels = 0;
            s_avi.audio_type = 0;
            return AVI_OK;
        }
        if (second->list_type != AVI_STRL_ID)
        {
            return AVI_STRL_ERR;
        }

        strh = (const strh_header_t *)(next + 12);
        if (strh->block_id != AVI_STRH_ID)
        {
            return AVI_STRH_ERR;
        }
        if (strh->stream_type != AVI_AUDS_STREAM)
        {
            return AVI_FORMAT_ERR;
        }
    }

    {
        const strf_wav_header_t *wav;

        wav = (const strf_wav_header_t *)avi_strf_after(next, strh);
        if (wav->block_id != AVI_STRF_ID)
        {
            return AVI_STRF_ERR;
        }
        s_avi.sample_rate = wav->sample_rate;
        s_avi.channels = wav->channels;
        s_avi.audio_type = wav->format_tag;
    }

    return AVI_OK;
}

avi_status_t avi_init(const uint8_t *block, uint16_t size)
{
    const avi_header_t *avi_header;
    const list_header_t *list_header;
    const avih_header_t *avih;
    const uint8_t *cursor;
    uint16_t cursor_size;
    uint16_t offset;
    avi_status_t res;

    memset(&s_avi, 0, sizeof(s_avi));

    avi_header = (const avi_header_t *)block;
    if (avi_header->riff_id != AVI_RIFF_ID)
    {
        return AVI_RIFF_ERR;
    }
    if (avi_header->avi_id != AVI_AVI_ID)
    {
        return AVI_AVI_ERR;
    }

    cursor = block + sizeof(avi_header_t);
    cursor_size = (uint16_t)(size - sizeof(avi_header_t));

    list_header = (const list_header_t *)cursor;
    if (list_header->list_id != AVI_LIST_ID)
    {
        return AVI_LIST_ERR;
    }
    if (list_header->list_type != AVI_HDRL_ID)
    {
        return AVI_HDRL_ERR;
    }

    cursor += sizeof(list_header_t);
    cursor_size = (uint16_t)(cursor_size - sizeof(list_header_t));

    avih = (const avih_header_t *)cursor;
    if (avih->block_id != AVI_AVIH_ID)
    {
        return AVI_AVIH_ERR;
    }
    s_avi.frame_interval_us = avih->sec_per_frame;
    s_avi.total_frames = avih->total_frames;

    cursor += avih->block_size + 8;
    cursor_size = (uint16_t)(cursor_size - (avih->block_size + 8));

    list_header = (const list_header_t *)cursor;
    if (list_header->list_id != AVI_LIST_ID)
    {
        return AVI_LIST_ERR;
    }
    if (list_header->list_type != AVI_STRL_ID)
    {
        return AVI_STRL_ERR;
    }

    res = avi_parse_streams(cursor, list_header);
    if (res != AVI_OK)
    {
        return res;
    }

    /* The movi list lives somewhere in this first block; the player needs its
     * offset to start reading chunks.  Search the whole buffer, like the
     * reference does. */
    offset = avi_search_id(block, size, "movi");
    if (offset == 0)
    {
        return AVI_MOVI_ERR;
    }

    if (s_avi.sample_rate != 0)
    {
        const uint8_t *movi;
        uint16_t audio_offset;

        movi = block + offset;
        audio_offset = avi_search_id(movi, (uint16_t)(size - offset), s_avi.audio_flag);
        if (audio_offset == 0)
        {
            return AVI_STREAM_ERR;
        }
        /* +4 skips the stream tag: the next two bytes are the chunk size. */
        movi += audio_offset + 4;
        s_avi.audio_buf_size = (uint16_t)(movi[0] | ((uint16_t)movi[1] << 8));
    }

    PRINT_LOG("[AVI ] %lux%lu frames=%lu us/frame=%lu\r\n",
              (unsigned long)s_avi.width, (unsigned long)s_avi.height,
              (unsigned long)s_avi.total_frames,
              (unsigned long)s_avi.frame_interval_us);
    PRINT_LOG("[AVI ] audio %lu Hz %u ch type=%u chunk=%u\r\n",
              (unsigned long)s_avi.sample_rate, (unsigned)s_avi.channels,
              (unsigned)s_avi.audio_type, (unsigned)s_avi.audio_buf_size);

    return AVI_OK;
}

uint16_t avi_search_id(const uint8_t *buf, uint16_t size, const char *id)
{
    uint16_t i;

    if (size < 4)
    {
        return 0;
    }
    size = (uint16_t)(size - 4);
    for (i = 0; i < size; i++)
    {
        if ((buf[i] == (uint8_t)id[0]) && (buf[i + 1] == (uint8_t)id[1]) &&
            (buf[i + 2] == (uint8_t)id[2]) && (buf[i + 3] == (uint8_t)id[3]))
        {
            return i;
        }
    }
    return 0;
}

avi_status_t avi_get_stream_info(const uint8_t *buf)
{
    s_avi.stream_id = (uint16_t)(buf[2] | ((uint16_t)buf[3] << 8));
    s_avi.stream_size = (uint32_t)buf[4] | ((uint32_t)buf[5] << 8) |
                        ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24);
    if ((s_avi.stream_size & 1u) != 0u)
    {
        s_avi.stream_size++;
    }

    if ((s_avi.stream_id == AVI_VIDS_FLAG) || (s_avi.stream_id == AVI_AUDS_FLAG))
    {
        return AVI_OK;
    }
    return AVI_STREAM_ERR;
}
