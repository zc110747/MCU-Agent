/**
 ******************************************************************************
 * @file    video_player.c
 * @brief   MJPEG/AVI player: playlist scan, demux loop, frame pacing, keys.
 *
 *          Ported from the ALIENTEK 实验47 video player.  Structural changes
 *          against the reference:
 *
 *            - the frame is decoded into an SDRAM surface and pushed to the
 *              panel through LVGL instead of being written into LCD GRAM from
 *              inside libjpeg's upsampler;
 *            - the four hand-rotated audio buffers became the SAI's own DMA
 *              double buffering (app/audio_pcm.c);
 *            - the directory entry index trick (dir_sdi + DIR.blk_ofs) is gone:
 *              the playlist is collected once with f_readdir and re-walked, so
 *              the player no longer depends on FatFs internals;
 *            - timing comes from the FreeRTOS tick, not from a TIM6 interrupt.
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "ff.h"

#include "video_player.h"
#include "video_avi.h"
#include "video_mjpeg.h"
#include "video_ui.h"
#include "audio_pcm.h"

#include "bsp_key.h"
#include "sd_card.h"
#include "fs_diskio.h"
#include "log.h"

/* Read window: one AVI chunk (JPEG frame or PCM block) plus its 8 byte header
 * and the following header.  60 KB matches the reference example. */
#define AVI_READ_WINDOW  (60U * 1024U)

#define PLAYLIST_MAX     32U
#define NAME_MAX_BYTES   64U

/** Seek step, in seconds of playback. */
#define SEEK_STEP_SEC    5U

typedef struct
{
    char names[PLAYLIST_MAX][NAME_MAX_BYTES];
    uint16_t count;
    uint16_t index;
} playlist_t;

typedef struct
{
    FIL *file;
    uint8_t *buf;
    uint32_t total_sec;
    uint32_t shown_sec;
    TickType_t frame_due;
    uint32_t frame_ms;
    uint8_t key_break;
} player_t;

static playlist_t s_list = {0};
static player_t s_play = {0};

/* ------------------------------------------------------------------ */
/* Playlist                                                           */
/* ------------------------------------------------------------------ */

static int8_t name_is_avi(const char *name)
{
    size_t len = strlen(name);

    if (len < 5U)
    {
        return 0;
    }
    if (name[len - 4] != '.')
    {
        return 0;
    }
    if ((name[len - 3] != 'a') && (name[len - 3] != 'A'))
    {
        return 0;
    }
    if ((name[len - 2] != 'v') && (name[len - 2] != 'V'))
    {
        return 0;
    }
    if ((name[len - 1] != 'i') && (name[len - 1] != 'I'))
    {
        return 0;
    }
    return 1;
}

static int8_t playlist_scan(const char *dir)
{
    DIR *dp;
    FILINFO *info;
    FRESULT res;
    int8_t status = 0;

    s_list.count = 0U;
    s_list.index = 0U;

    dp = (DIR *)pvPortMalloc(sizeof(DIR));
    info = (FILINFO *)pvPortMalloc(sizeof(FILINFO));
    if ((dp == NULL) || (info == NULL))
    {
        vPortFree(dp);
        vPortFree(info);
        return 1;
    }

    fs_lock();
    res = f_opendir(dp, dir);
    if (res == FR_OK)
    {
        while (s_list.count < PLAYLIST_MAX)
        {
            res = f_readdir(dp, info);
            if ((res != FR_OK) || (info->fname[0] == '\0'))
            {
                break;
            }
            if (name_is_avi(info->fname) == 0)
            {
                continue;
            }
            strncpy(s_list.names[s_list.count], info->fname, NAME_MAX_BYTES - 1U);
            s_list.names[s_list.count][NAME_MAX_BYTES - 1U] = '\0';
            s_list.count++;
        }
        (void)f_closedir(dp);
    }
    fs_unlock();

    vPortFree(dp);
    vPortFree(info);

    PRINT_LOG("[VID ] %s -> %u file(s)\r\n", dir, (unsigned)s_list.count);
    if (s_list.count == 0U)
    {
        status = 1;
    }
    return status;
}

/* ------------------------------------------------------------------ */
/* Playback helpers                                                   */
/* ------------------------------------------------------------------ */

static void format_hms(uint32_t sec, char *out)
{
    (void)sprintf(out, "%02lu:%02lu:%02lu", (unsigned long)(sec / 3600U),
                  (unsigned long)((sec % 3600U) / 60U), (unsigned long)(sec % 60U));
}

/** Frame counter arithmetic: keep the frame slot grid, drop missed slots. */
static void frame_pace(void)
{
    TickType_t now = xTaskGetTickCount();
    TickType_t period = pdMS_TO_TICKS(s_play.frame_ms);

    s_play.frame_due += period;
    if ((int32_t)(s_play.frame_due - now) > 0)
    {
        vTaskDelay(s_play.frame_due - now);
    }
    else
    {
        /* Decoding overran the slot: resynchronise instead of bursting. */
        s_play.frame_due = now;
    }
}

static void show_progress(void)
{
    char line[48];
    char cur[16];
    char total[16];
    uint32_t now_sec;
    uint32_t size;

    size = (uint32_t)f_size(s_play.file);
    if (size == 0U)
    {
        return;
    }
    now_sec = (uint32_t)(((uint64_t)f_tell(s_play.file) * s_play.total_sec) / size);
    if (now_sec == s_play.shown_sec)
    {
        return;
    }
    s_play.shown_sec = now_sec;

    format_hms(now_sec, cur);
    format_hms(s_play.total_sec, total);
    (void)sprintf(line, "%s/%s  %lu fps", cur, total,
                  (unsigned long)(1000U / s_play.frame_ms));
    video_ui_set_status(line);
}

/**
 * @brief  Jump by @p seconds (positive = forward) and realign on the next
 *         video chunk header.
 */
static void player_seek(int32_t seconds)
{
    FSIZE_t size = f_size(s_play.file);
    FSIZE_t pos = f_tell(s_play.file);
    FSIZE_t delta;
    UINT br;
    uint16_t offset;

    if ((s_play.total_sec == 0U) || (size == 0U))
    {
        return;
    }

    delta = (size / (FSIZE_t)s_play.total_sec) * (FSIZE_t)(seconds < 0 ? -seconds : seconds);
    if (seconds > 0)
    {
        pos = (pos + delta < size) ? (pos + delta) : size;
    }
    else
    {
        pos = (pos > delta) ? (pos - delta) : 0U;
    }

    audio_pcm_close();

    if ((f_lseek(s_play.file, pos) != FR_OK) ||
        (f_read(s_play.file, s_play.buf, AVI_READ_WINDOW, &br) != FR_OK))
    {
        return;
    }

    offset = 0U;
    if (pos == 0U)
    {
        offset = avi_search_id(s_play.buf, (uint16_t)br, "movi");
    }
    offset = (uint16_t)(offset + avi_search_id(s_play.buf + offset,
                                              (uint16_t)(br - offset),
                                              avi_info()->video_flag));
    if ((offset == 0U) || (offset >= br))
    {
        return;
    }

    (void)avi_get_stream_info(s_play.buf + offset);
    (void)f_lseek(s_play.file, pos + offset + 8);
    s_play.shown_sec = 0xFFFFFFFFu;
}

/* ------------------------------------------------------------------ */
/* One file                                                           */
/* ------------------------------------------------------------------ */

static int8_t player_run_file(const char *path, const char *name,
                              uint16_t index, uint16_t total)
{
    const avi_info_t *info;
    uint16_t panel_w = 0U;
    uint16_t panel_h = 0U;
    uint16_t off_x;
    uint16_t off_y;
    UINT br = 0U;
    uint16_t movi_offset;
    avi_status_t res;
    uint8_t status = 0U;

    s_play.file = (FIL *)pvPortMalloc(sizeof(FIL));
    s_play.buf = (uint8_t *)pvPortMalloc(AVI_READ_WINDOW);
    if ((s_play.file == NULL) || (s_play.buf == NULL))
    {
        PRINT_LOG("[VID ] buffer alloc FAILED\r\n");
        status = 1U;
        goto cleanup;
    }

    fs_lock();
    res = (f_open(s_play.file, path, FA_READ) == FR_OK) ? AVI_OK : AVI_RIFF_ERR;
    fs_unlock();
    if (res != AVI_OK)
    {
        PRINT_LOG("[VID ] open FAILED: %s\r\n", path);
        status = 1U;
        goto cleanup;
    }

    fs_lock();
    res = (f_read(s_play.file, s_play.buf, AVI_READ_WINDOW, &br) == FR_OK)
              ? AVI_OK
              : AVI_RIFF_ERR;
    fs_unlock();
    if ((res != AVI_OK) || (br < 64U))
    {
        PRINT_LOG("[VID ] header read FAILED\r\n");
        status = 1U;
        goto cleanup;
    }

    res = avi_init(s_play.buf, (uint16_t)br);
    if (res != AVI_OK)
    {
        PRINT_LOG("[VID ] AVI parse error %u\r\n", (unsigned)res);
        status = 1U;
        goto cleanup;
    }

    info = avi_info();
    s_play.frame_ms = info->frame_interval_us / 1000U;
    if (s_play.frame_ms == 0U)
    {
        s_play.frame_ms = 66U;
    }
    s_play.total_sec = (info->frame_interval_us * info->total_frames) / 1000000U;
    s_play.shown_sec = 0xFFFFFFFFu;
    s_play.key_break = 0U;

    video_ui_surface_size(&panel_w, &panel_h);
    off_x = (uint16_t)((panel_w > info->width) ? ((panel_w - info->width) / 2U) : 0U);
    off_y = (uint16_t)(VIDEO_UI_TOP_BAND +
                       (((panel_h - VIDEO_UI_TOP_BAND) > info->height)
                            ? (((panel_h - VIDEO_UI_TOP_BAND) - info->height) / 2U)
                            : 0U));

    if (video_mjpeg_open(video_ui_surface(), panel_w, panel_h, off_x, off_y) != 0)
    {
        PRINT_LOG("[VID ] decoder init FAILED\r\n");
        status = 1U;
        goto cleanup;
    }

    video_ui_set_title(name, index, total);

    if ((info->sample_rate != 0U) && (info->audio_type == AVI_AUDIO_PCM))
    {
        if (audio_pcm_open(info->sample_rate, info->channels) != 0)
        {
            PRINT_LOG("[VID ] audio disabled (codec/SAI init failed)\r\n");
        }
    }
    else if (info->sample_rate != 0U)
    {
        PRINT_LOG("[VID ] audio type %u is not PCM, muted\r\n", (unsigned)info->audio_type);
    }

    /* Position on the first chunk after the movi list header. */
    movi_offset = avi_search_id(s_play.buf, (uint16_t)br, "movi");
    (void)avi_get_stream_info(s_play.buf + movi_offset + 4);

    fs_lock();
    res = (f_lseek(s_play.file, (FSIZE_t)movi_offset + 12U) == FR_OK) ? AVI_OK
                                                                    : AVI_STREAM_ERR;
    fs_unlock();
    if (res != AVI_OK)
    {
        status = 1U;
        goto cleanup;
    }

    s_play.frame_due = xTaskGetTickCount();

    while (s_play.key_break == 0U)
    {
        uint32_t chunk = info->stream_size;

        fs_lock();
        res = (f_read(s_play.file, s_play.buf, chunk + 8U, &br) == FR_OK) ? AVI_OK
                                                                        : AVI_STREAM_ERR;
        fs_unlock();
        if ((res != AVI_OK) || (br < chunk + 8U))
        {
            PRINT_LOG("[VID ] read stop at %lu bytes\r\n", (unsigned long)f_tell(s_play.file));
            break;
        }

        if (info->stream_id == AVI_VIDS_FLAG)
        {
            if (video_mjpeg_decode(s_play.buf, chunk) == 0)
            {
                video_ui_frame_ready();
            }
            show_progress();
            frame_pace();
        }
        else
        {
            (void)audio_pcm_write(s_play.buf, chunk);
            show_progress();
        }

        if (bsp_key_edge(BSP_KEY_0) != 0U)
        {
            s_play.key_break = 1U; /* next file */
        }
        else if (bsp_key_edge(BSP_KEY_2) != 0U)
        {
            s_play.key_break = 2U; /* previous file */
        }
        else if (bsp_key_edge(BSP_KEY_UP) != 0U)
        {
            player_seek((int32_t)SEEK_STEP_SEC);
        }
        else if (bsp_key_edge(BSP_KEY_1) != 0U)
        {
            player_seek(-(int32_t)SEEK_STEP_SEC);
        }

        /* The following chunk header sits right after this chunk's payload. */
        if (avi_get_stream_info(s_play.buf + chunk) != AVI_OK)
        {
            PRINT_LOG("[VID ] stream tag lost, file ends\r\n");
            break;
        }
    }

cleanup:
    video_mjpeg_close();
    audio_pcm_close();

    if (s_play.file != NULL)
    {
        fs_lock();
        (void)f_close(s_play.file);
        fs_unlock();
        vPortFree(s_play.file);
        s_play.file = NULL;
    }
    if (s_play.buf != NULL)
    {
        vPortFree(s_play.buf);
        s_play.buf = NULL;
    }

    return (int8_t)status;
}

/* ------------------------------------------------------------------ */
/* Task                                                               */
/* ------------------------------------------------------------------ */

void video_player_task(void *arg)
{
    char path[NAME_MAX_BYTES + 16U];
    uint8_t playlist_ready = 0U;

    (void)arg;

    /* The screenshot surface and the card are brought up by the UI task; wait
     * for both instead of duplicating that sequence here. */
    while ((video_ui_ready() == 0U) || (sd_card_is_ready() == 0))
    {
        vTaskDelay(pdMS_TO_TICKS(100U));
    }

    while (playlist_ready == 0U)
    {
        if (playlist_scan("0:/VIDEO") == 0)
        {
            playlist_ready = 1U;
        }
        else
        {
            video_ui_set_status("no AVI in 0:/VIDEO");
            vTaskDelay(pdMS_TO_TICKS(1000U));
        }
    }

    for (;;)
    {
        (void)snprintf(path, sizeof(path), "0:/VIDEO/%s", s_list.names[s_list.index]);
        PRINT_LOG("[VID ] play %s\r\n", path);

        (void)player_run_file(path, s_list.names[s_list.index],
                              (uint16_t)(s_list.index + 1U), s_list.count);

        if (s_play.key_break == 2U)
        {
            s_list.index = (s_list.index == 0U) ? (uint16_t)(s_list.count - 1U)
                                                : (uint16_t)(s_list.index - 1U);
        }
        else
        {
            s_list.index++;
            if (s_list.index >= s_list.count)
            {
                s_list.index = 0U;
            }
        }

        /* A removed card invalidates the playlist; rebuild it when it returns. */
        if (sd_card_is_ready() == 0)
        {
            playlist_ready = 0U;
            while (sd_card_is_ready() == 0)
            {
                vTaskDelay(pdMS_TO_TICKS(500U));
            }
            while (playlist_ready == 0U)
            {
                playlist_ready = (playlist_scan("0:/VIDEO") == 0) ? 1U : 0U;
                if (playlist_ready == 0U)
                {
                    vTaskDelay(pdMS_TO_TICKS(1000U));
                }
            }
        }
    }
}
