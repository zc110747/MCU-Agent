/**
 ******************************************************************************
 * @file    audio_player.c
 * @brief   Playback engine: playlist walking, SAI double buffering, keys.
 *
 *  One task owns the whole audio path.  It blocks on a DMA transfer-complete
 *  semaphore and refills whichever half buffer just finished, so no FatFs call
 *  ever happens in interrupt context.
 *
 *  Codec agnostic
 *  --------------
 *  Everything below track_decoder.h is just "16-bit stereo PCM at Fs": WAV is
 *  unpacks/expands, MP3 is decoded by minimp3, and the SAI is always
 *  configured for 16-bit slots.  Adding a format therefore never touches this
 *  file.
 *
 *  Buffer strategy
 *  ---------------
 *  Two 4 KB halves driven by DMA2 Stream3 in double-buffer mode.  At 44.1 kHz
 *  stereo 16-bit one half is ~23 ms of audio, which is three orders of
 *  magnitude longer than a polled SDIO read of the same size -- so refilling
 *  from the task, never from the ISR, costs no audio continuity.
 *
 *  Why the ISR records the free half
 *  ---------------------------------
 *  bsp_sai_free_buffer() reads the DMA's CT bit.  If the task were delayed by
 *  more than one buffer period, CT would already have flipped again and the
 *  task would refill the half that is *playing*.  Sampling CT inside the ISR
 *  and handing that index to the task keeps the pairing exact.
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "ff.h"

#include "audio_player.h"
#include "bsp_sai.h"
#include "bsp_wm8978.h"
#include "playlist.h"
#include "sd_card.h"
#include "track_decoder.h"
#include "fs_diskio.h"
#include "log.h"

/* One DMA half buffer.  4 KB is what the ALIENTEK reference uses; it is also
 * the smallest size that stayed glitch-free at 24-bit / 192 kHz there. */
#define AUDIO_BUF_BYTES 4096U
/* The task stack also carries a FIL (with its long-filename buffer), the path
 * and the decoder's return path, so 6 KB rather than the 3 KB a WAV-only player
 * needed.  The MP3 decoder's own 16 KB scratch buffer deliberately does NOT
 * live here - see the local patch note in third_party/minimp3/minimp3.h. */
#define AUDIO_TASK_STACK 1536U
#define AUDIO_TASK_PRIO (tskIDLE_PRIORITY + 3)

typedef enum {
    TRACK_RESULT_NEXT = 0,
    TRACK_RESULT_PREV,
    TRACK_RESULT_ERROR,
} TRACK_RESULT_T;

typedef struct
{
    AUDIO_STATUS_T   status;
    volatile bool    req_next;
    volatile bool    req_prev;
    volatile uint8_t free_half; /* written by the DMA ISR */
} AUDIO_RUNTIME_T;

static AUDIO_RUNTIME_T s_audio = {0};

static uint8_t s_buf0[AUDIO_BUF_BYTES] __attribute__((aligned(32)));
static uint8_t s_buf1[AUDIO_BUF_BYTES] __attribute__((aligned(32)));

static SemaphoreHandle_t s_dma_sem = NULL;

/* -------------------------------------------------------------------------- */
/* DMA completion (interrupt context, priority 6)                             */
/* -------------------------------------------------------------------------- */
static void audio_dma_complete(void)
{
    BaseType_t woken = pdFALSE;

    s_audio.free_half = bsp_sai_free_buffer();
    (void)xSemaphoreGiveFromISR(s_dma_sem, &woken);
    portYIELD_FROM_ISR(woken);
}

/* -------------------------------------------------------------------------- */
/* FatFs helpers (track_decoder takes the filesystem lock itself)              */
/* -------------------------------------------------------------------------- */
static uint32_t read_pcm(uint8_t *dst, uint32_t bytes)
{
    return track_decoder_read(dst, bytes);
}

static void publish_track(const TRACK_INFO_T *info)
{
    const char *name = playlist_name((uint16_t)playlist_current());

    s_audio.status.track           = playlist_current();
    s_audio.status.total_seconds   = info->total_seconds;
    s_audio.status.current_seconds = 0U;
    s_audio.status.sample_rate     = info->sample_rate;
    s_audio.status.bits_per_sample = info->bits_per_sample;
    s_audio.status.is_mp3          = (info->kind == TRACK_KIND_MP3) ? 1U : 0U;
    (void)snprintf(s_audio.status.name, sizeof(s_audio.status.name), "%s",
                   (name != NULL) ? name : "?");
}

/* -------------------------------------------------------------------------- */
/* One track                                                                  */
/* -------------------------------------------------------------------------- */
static TRACK_RESULT_T play_one_track(void)
{
    FIL            fp;
    TRACK_INFO_T   info;
    FRESULT        rc;
    char           path[PLAYLIST_NAME_MAX + 16U];
    uint32_t       filled0;
    uint32_t       filled1;
    TRACK_RESULT_T result = TRACK_RESULT_NEXT;

    if (playlist_current() < 0)
    {
        (void)playlist_select(0U);
    }
    if (!playlist_path((uint16_t)playlist_current(), path, sizeof(path)))
    {
        return TRACK_RESULT_ERROR;
    }

    fs_lock();
    rc = f_open(&fp, path, FA_READ);
    fs_unlock();
    if (rc != FR_OK)
    {
        PRINT_LOG("[PLAY] open failed rc=%d : %s\r\n", (int32_t)rc, path);
        return TRACK_RESULT_ERROR;
    }

    if (!track_decoder_open(&fp, path, &info))
    {
        track_decoder_close();
        fs_lock();
        (void)f_close(&fp);
        fs_unlock();
        return TRACK_RESULT_ERROR;
    }

    /* The whole chain is 16-bit stereo now, whatever the source was. */
    bsp_wm8978_set_i2s_format(WM8978_FMT_I2S, WM8978_WLEN_16);

    if (!bsp_sai_configure(BSP_SAI_BITS_16, info.sample_rate,
                           s_buf0, s_buf1, AUDIO_BUF_BYTES))
    {
        track_decoder_close();
        fs_lock();
        (void)f_close(&fp);
        fs_unlock();
        return TRACK_RESULT_ERROR;
    }

    /* Drop any completion left over from the previous track. */
    while (xSemaphoreTake(s_dma_sem, 0) == pdTRUE)
    {
    }

    publish_track(&info);

    filled0 = read_pcm(s_buf0, AUDIO_BUF_BYTES);
    filled1 = read_pcm(s_buf1, AUDIO_BUF_BYTES);

    if ((filled0 | filled1) == 0U)
    {
        /* Empty data chunk: skip without starting the clock. */
        track_decoder_close();
        fs_lock();
        (void)f_close(&fp);
        fs_unlock();
        return TRACK_RESULT_NEXT;
    }

    s_audio.status.state = AUDIO_STATE_PLAYING;
    bsp_sai_play();

    PRINT_LOG("[PLAY] now playing: %s\r\n", s_audio.status.name);

    for (;;)
    {
        uint8_t *half;
        uint32_t filled;

        if (xSemaphoreTake(s_dma_sem, pdMS_TO_TICKS(200)) != pdTRUE)
        {
            /* No completion for 200 ms: the stream is longer than any half buffer at
             * the slowest supported rate, so this can only be a stalled clock. */
            PRINT_LOG("[PLAY] DMA stalled, aborting track\r\n");
            result = TRACK_RESULT_ERROR;
            break;
        }

        if (s_audio.req_prev)
        {
            s_audio.req_prev = false;
            result           = TRACK_RESULT_PREV;
            break;
        }
        if (s_audio.req_next)
        {
            s_audio.req_next = false;
            result           = TRACK_RESULT_NEXT;
            break;
        }

        half = (s_audio.free_half == 0U) ? s_buf0 : s_buf1;

        if (s_audio.status.state == AUDIO_STATE_PAUSED)
        {
            /* Keep the clock fed with silence: resuming is then instantaneous. */
            memset(half, 0, AUDIO_BUF_BYTES);
            continue;
        }

        filled = read_pcm(half, AUDIO_BUF_BYTES);
        if (filled < AUDIO_BUF_BYTES)
        {
            /* End of file: the tail is already zero-padded, so just let the last half
             * finish playing before switching tracks. */
            uint32_t bytes_per_sec = track_decoder_output_rate();

            if (bytes_per_sec != 0U)
            {
                vTaskDelay(pdMS_TO_TICKS((filled * 1000U) / bytes_per_sec + 10U));
            }
            PRINT_LOG("[PLAY] end of track, auto next\r\n");
            result = TRACK_RESULT_NEXT;
            break;
        }

        s_audio.status.current_seconds = track_decoder_position_seconds();
    }

    if (info.kind == TRACK_KIND_MP3)
    {
        /* Decode cost vs. audio produced: the headroom number that decides
         * whether a faster MP3 rate is still playable on this clock. */
        uint32_t played = track_decoder_position_seconds();
        uint32_t used   = track_decoder_decode_ms();

        PRINT_LOG("[MP3 ] decoded %lu s in %lu ms (%lu%% of realtime)\r\n",
                  (unsigned long)played, (unsigned long)used,
                  (unsigned long)((played != 0U) ? ((used * 100U) / (played * 1000U)) : 0U));
    }

    bsp_sai_stop();
    track_decoder_close();
    fs_lock();
    (void)f_close(&fp);
    fs_unlock();
    return result;
}

/* -------------------------------------------------------------------------- */
/* Task                                                                       */
/* -------------------------------------------------------------------------- */
/* Consecutive unreadable tracks tolerated before the volume is declared dead.
 * One bad file must not cost a remount, but FatFs sticks on FR_DISK_ERR until
 * the volume is mounted again - so a whole-table failure has to escalate. */
#define AUDIO_ERROR_LIMIT 3U

static void audio_task(void *arg)
{
    (void)arg;

    uint8_t error_streak = 0U;

    for (;;)
    {
        if (sd_card_is_ready() == 0)
        {
            if (sd_card_init() != RT_OK)
            {
                s_audio.status.state = AUDIO_STATE_ERROR;
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
        }

        if (playlist_count() == 0U)
        {
            if (!playlist_scan())
            {
                s_audio.status.state = AUDIO_STATE_ERROR;
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            (void)playlist_select(0U);
        }

        s_audio.status.track_count = playlist_count();
        s_audio.status.dir_files   = playlist_dir_files();

        {
            TRACK_RESULT_T result = play_one_track();

            if (result == TRACK_RESULT_ERROR)
            {
                error_streak++;

                if (error_streak >= AUDIO_ERROR_LIMIT)
                {
                    /* Every entry failed: assume the card, not the files.
                     * Unmount + drop the table so the top of this loop brings
                     * the whole stack back up instead of logging forever. */
                    PRINT_LOG("[PLAY] %u track(s) unreadable, remounting SD\r\n",
                              (unsigned)error_streak);
                    playlist_clear();
                    sd_card_invalidate();
                    error_streak = 0U;
                    vTaskDelay(pdMS_TO_TICKS(500));
                    continue;
                }

                (void)playlist_step(1);
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }

            error_streak = 0U;

            if (result == TRACK_RESULT_PREV)
            {
                (void)playlist_step(-1);
            }
            else
            {
                (void)playlist_step(1);
            }
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */
void audio_player_start(void)
{
    if (s_dma_sem != NULL)
    {
        return;
    }

    s_dma_sem = xSemaphoreCreateBinary();
    configASSERT(s_dma_sem != NULL);

    s_audio.status.state = AUDIO_STATE_IDLE;
    s_audio.status.track = -1;

    bsp_sai_set_dma_callback(audio_dma_complete);

    (void)xTaskCreate(audio_task, "audio", AUDIO_TASK_STACK, NULL,
                      AUDIO_TASK_PRIO, NULL);
}

const AUDIO_STATUS_T *audio_player_status(void)
{
    return &s_audio.status;
}

void audio_player_toggle_pause(void)
{
    if (s_audio.status.state == AUDIO_STATE_PLAYING)
    {
        s_audio.status.state = AUDIO_STATE_PAUSED;
        PRINT_LOG("[KEY ] KEY_UP -> pause at %us\r\n",
                  (unsigned)s_audio.status.current_seconds);
    }
    else if (s_audio.status.state == AUDIO_STATE_PAUSED)
    {
        s_audio.status.state = AUDIO_STATE_PLAYING;
        PRINT_LOG("[KEY ] KEY_UP -> resume at %us\r\n",
                  (unsigned)s_audio.status.current_seconds);
    }
}

void audio_player_next(void)
{
    s_audio.req_next = true;
    PRINT_LOG("[KEY ] KEY0 -> next track\r\n");
}

void audio_player_prev(void)
{
    s_audio.req_prev = true;
    PRINT_LOG("[KEY ] KEY2 -> previous track\r\n");
}
