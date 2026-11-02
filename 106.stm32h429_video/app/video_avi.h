/**
 ******************************************************************************
 * @file    video_avi.h
 * @brief   Minimal AVI (RIFF) demuxer for MJPEG + PCM files, ported from the
 *          ALIENTEK 实验47 video player example.
 *
 *          Only what a player needs is parsed: frame timing, picture size,
 *          audio format and the two stream tags ("00dc"/"01dc", "00wb"/"01wb").
 *          Everything is read from the first 60 KB block of the file, so all
 *          accessors work on that one buffer.
 ******************************************************************************
 */
#ifndef VIDEO_AVI_H
#define VIDEO_AVI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Four character codes, little endian as stored in the file. */
#define AVI_RIFF_ID       0x46464952u
#define AVI_AVI_ID        0x20495641u
#define AVI_LIST_ID       0x5453494Cu
#define AVI_HDRL_ID       0x6C726468u
#define AVI_MOVI_ID       0x69766F6Du
#define AVI_STRL_ID       0x6C727473u
#define AVI_AVIH_ID       0x68697661u
#define AVI_STRH_ID       0x68727473u
#define AVI_STRF_ID       0x66727473u

#define AVI_VIDS_STREAM   0x73646976u
#define AVI_AUDS_STREAM   0x73647561u
#define AVI_FORMAT_MJPG   0x47504A4Du

/* Stream id of a data chunk (chars 2..3 of "00dc"/"00wb"). */
#define AVI_VIDS_FLAG     0x6463u
#define AVI_AUDS_FLAG     0x7762u

/* PCM is the only audio format the player accepts. */
#define AVI_AUDIO_PCM     0x0001u

typedef enum
{
    AVI_OK = 0,
    AVI_RIFF_ERR,
    AVI_AVI_ERR,
    AVI_LIST_ERR,
    AVI_HDRL_ERR,
    AVI_AVIH_ERR,
    AVI_STRL_ERR,
    AVI_STRH_ERR,
    AVI_STRF_ERR,
    AVI_MOVI_ERR,
    AVI_FORMAT_ERR,
    AVI_STREAM_ERR,
} avi_status_t;

typedef struct
{
    uint32_t frame_interval_us; /* avih dwMicroSecPerFrame */
    uint32_t total_frames;      /* avih dwTotalFrames       */
    uint32_t width;             /* strf biWidth             */
    uint32_t height;            /* strf biHeight            */
    uint32_t sample_rate;       /* strf nSamplesPerSec, 0 = no audio */
    uint16_t channels;          /* strf nChannels           */
    uint16_t audio_buf_size;    /* first audio chunk size (incl. 8 byte header) */
    uint16_t audio_type;        /* WAVE format tag, 1 = PCM */
    uint16_t stream_id;         /* 'dc' / 'wb' of the chunk just parsed         */
    uint32_t stream_size;       /* payload size of that chunk, padded to even   */
    const char *video_flag;     /* "00dc" or "01dc" */
    const char *audio_flag;     /* "00wb" or "01wb" */
} avi_info_t;

/** Header fields of the file currently open. */
const avi_info_t *avi_info(void);

/**
 * @brief  Parse the RIFF header block read from the start of the file.
 * @param  buf   first block of the file (>= a few hundred bytes)
 * @param  size  bytes valid in @p buf
 */
avi_status_t avi_init(const uint8_t *buf, uint16_t size);

/**
 * @brief  Find a 4 character code inside @p buf.
 * @retval byte offset of the code, 0 when absent
 */
uint16_t avi_search_id(const uint8_t *buf, uint16_t size, const char *id);

/**
 * @brief  Decode the 8 byte chunk header that @p buf points at (it starts with
 *         the stream tag, e.g. "00dc") and publish stream_id / stream_size.
 */
avi_status_t avi_get_stream_info(const uint8_t *buf);

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_AVI_H */
