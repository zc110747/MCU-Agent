/**
  ******************************************************************************
  * @file    app/audio_player.c
  * @brief   Music player engine (see audio_player.h).
  ******************************************************************************
  */
#include "audio_player.h"
#include "audio_decoder.h"
#include "bsp_wm8978.h"
#include "bsp_sai_audio.h"
#include "ff.h"
#include "log.h"

#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* ---- Static state --------------------------------------------------------- */
static track_t        s_tracks[PLAYER_MAX_TRACKS];
static uint32_t       s_count = 0U;
static int            s_cur   = 0;
static player_state_t s_state = PLAYER_STOPPED;
static uint8_t        s_vol   = 70U;
static dec_ctx_t      s_dec;
static uint8_t        s_inited = 0U;

/* ---- Player lock ----------------------------------------------------------
 * Guards the shared decoder context (s_dec) and player state (s_state/s_cur)
 * against the UI thread (button callbacks run in the LVGL task) and the audio
 * refill task.  A recursive mutex lets player_toggle() nest play/pause/load.
 * Without this, a "next/prev" press while playing closed & zeroed s_dec while
 * the audio task was mid-decode -> NULL deref -> HardFault -> whole board
 * freezes.  See verify notes: this was the click-to-freeze bug. */
static SemaphoreHandle_t s_lock = NULL;
#define PLOCK_TAKE() do { if (s_lock != NULL) (void)xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); } while (0)
#define PLOCK_GIVE() do { if (s_lock != NULL) (void)xSemaphoreGiveRecursive(s_lock); } while (0)

/* ---- Helpers -------------------------------------------------------------- */
static void silence_half(int16_t *base, uint32_t from, uint32_t half)
{
    if (from < half)
    {
        (void)memset(base + (size_t)from * 2U, 0,
                     (size_t)(half - from) * 2U * sizeof(int16_t));
    }
}

/* Decode both halves of the SAI buffer and (re)start the DMA.  Because no
 * refill happens while paused, the decoder position is unchanged, so this
 * also reproduces exact continuity on resume. */
static void player_prime_and_start(void)
{
    uint16_t *buf = sai_audio_buffer();
    uint32_t  hf  = sai_audio_half_frames();
    uint32_t  g0, g1;

    audio_decoder_read(&s_dec, (int16_t *)buf, hf, &g0);
    silence_half((int16_t *)buf, g0, hf);

    audio_decoder_read(&s_dec, (int16_t *)buf + (size_t)hf * 2U, hf, &g1);
    silence_half((int16_t *)buf + (size_t)hf * 2U, g1, hf);

    sai_audio_start();
    s_state = ((g0 == 0U) && (g1 == 0U)) ? PLAYER_STOPPED : PLAYER_PLAYING;
}

/* Open track idx and (optionally) start playing it.  Caller must hold s_lock. */
static int player_load_locked(uint32_t idx, uint8_t autoplay)
{
    if (idx >= s_count)
    {
        return -1;
    }

    audio_decoder_close(&s_dec);
    sai_audio_stop();
    sai_audio_drain();

    if (audio_decoder_open(&s_dec, s_tracks[idx].path) != 0)
    {
        PRINT_LOG("[PLY ] open failed: %s\r\n", s_tracks[idx].path);
        s_cur = (int)idx;
        s_state = PLAYER_STOPPED;
        return -1;
    }

    sai_audio_configure(s_dec.sample_rate);
    s_cur = (int)idx;

    if (autoplay != 0U)
    {
        player_prime_and_start();
    }
    else
    {
        s_state = PLAYER_PAUSED; /* loaded, not sounding */
    }
    return 0;
}

/* Public wrapper: take the player lock, then delegate to player_load_locked. */
int player_load(uint32_t idx, uint8_t autoplay)
{
    int r;
    PLOCK_TAKE();
    r = player_load_locked(idx, autoplay);
    PLOCK_GIVE();
    return r;
}

/* Called from the refill loop when the current track ends.  Walks forward
 * until a playable track is found (or the list is exhausted). */
static int player_advance_auto(void)
{
    uint32_t i;

    if (s_count == 0U)
    {
        s_state = PLAYER_STOPPED;
        sai_audio_stop();
        sai_audio_drain();
        return -1;
    }

    for (i = 1U; i <= s_count; i++)
    {
        uint32_t idx = (s_cur + i) % s_count;
        if ((player_load(idx, 1U) == 0) && (s_state == PLAYER_PLAYING))
        {
            return 0;
        }
    }

    s_state = PLAYER_STOPPED;
    sai_audio_stop();
    sai_audio_drain();
    return -1;
}

/* ---- Refill task ---------------------------------------------------------- */
static void player_task(void *arg)
{
    (void)arg;

    for (;;)
    {
        if ((s_state == PLAYER_PLAYING) && (s_dec.opened != 0U))
        {
            /* Wait for a half-buffer to need filling WITHOUT holding the lock,
             * so a UI command (which also needs the lock) is never blocked
             * behind this task waiting for the DMA. */
            uint8_t  half = sai_audio_get_empty_half();
            uint32_t hf   = sai_audio_half_frames();
            int16_t *p    = (int16_t *)sai_audio_buffer() +
                            (size_t)half * (size_t)hf * 2U;
            uint32_t got;

            PLOCK_TAKE();
            /* Re-check under the lock: a UI command may have stopped/paused or
             * switched tracks while we were parked on the DMA semaphore. */
            if ((s_state == PLAYER_PLAYING) && (s_dec.opened != 0U))
            {
                audio_decoder_read(&s_dec, p, hf, &got);
                if (got < hf)
                {
                    silence_half(p, got, hf);
                    (void)player_advance_auto();
                }
            }
            PLOCK_GIVE();
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(40));
        }
    }
}

/* ---- Public API ----------------------------------------------------------- */
int player_init(void)
{
    if (s_inited != 0U)
    {
        return 0;
    }

    if (s_lock == NULL)
    {
        s_lock = xSemaphoreCreateRecursiveMutex();
        if (s_lock == NULL)
        {
            PRINT_LOG("[PLY ] player mutex create failed\r\n");
            return -1;
        }
    }

    if (wm8978_init() != 0)
    {
        PRINT_LOG("[PLY ] WM8978 init failed\r\n");
        return -1;
    }
    if (sai_audio_init(44100U, 2U, 16U) != 0)
    {
        PRINT_LOG("[PLY ] SAI init failed\r\n");
        return -1;
    }

    s_vol = 70U;
    wm8978_set_volume(s_vol);

    if (xTaskCreate(player_task, "audio", 2048, NULL,
                    tskIDLE_PRIORITY + 1, NULL) != pdPASS)
    {
        PRINT_LOG("[PLY ] task create failed\r\n");
        return -1;
    }

    s_inited = 1U;
    PRINT_LOG("[PLY ] init OK\r\n");
    return 0;
}

uint32_t player_scan(const char *vol)
{
    DIR dir;
    FILINFO fno;
    char music_dir[32];
    FRESULT fr;
    uint32_t n = 0U;

    s_count = 0U;
    (void)memset(s_tracks, 0, sizeof(s_tracks));

    (void)snprintf(music_dir, sizeof(music_dir), "%s/music", vol);

    fr = f_opendir(&dir, music_dir);
    if (fr != FR_OK)
    {
        PRINT_LOG("[PLY ] opendir %s failed (%d)\r\n", music_dir, (int)fr);
        return 0U;
    }

    for (;;)
    {
        fr = f_readdir(&dir, &fno);
        if ((fr != FR_OK) || (fno.fname[0] == '\0'))
        {
            break;
        }
        if ((fno.fattrib & AM_DIR) != 0U)
        {
            continue; /* skip sub-directories */
        }

        {
            size_t pl = strlen(fno.fname);
            const char *dot = (pl >= 4U) ? (fno.fname + pl - 4U) : NULL;
            int is_audio = 0;
            if (dot != NULL)
            {
                if ((dot[0] == '.') &&
                    (dot[1] == 'w' || dot[1] == 'W') &&
                    (dot[2] == 'a' || dot[2] == 'A') &&
                    (dot[3] == 'v' || dot[3] == 'V'))
                {
                    is_audio = 1;
                }
                else if ((dot[0] == '.') &&
                         (dot[1] == 'm' || dot[1] == 'M') &&
                         (dot[2] == 'p' || dot[2] == 'P') &&
                         (dot[3] == '3'))
                {
                    is_audio = 1;
                }
            }
            if (!is_audio)
            {
                continue;
            }
        }

        if (n >= PLAYER_MAX_TRACKS)
        {
            break;
        }
        (void)snprintf(s_tracks[n].path, sizeof(s_tracks[n].path),
                       "%s/%s", music_dir, fno.fname);
        (void)snprintf(s_tracks[n].name, sizeof(s_tracks[n].name),
                       "%.*s", (int)(sizeof(s_tracks[n].name) - 1U),
                       fno.fname);
        n++;
    }
    f_closedir(&dir);

    s_count = n;
    s_cur   = 0;
    PRINT_LOG("[PLY ] scanned %lu track(s) in %s\r\n",
              (unsigned long)n, music_dir);
    return n;
}

uint32_t player_count(void)        { return s_count; }
player_state_t player_state(void)  { return s_state; }

int player_play(void)
{
    int r;
    PLOCK_TAKE();
    if (s_count == 0U)
    {
        r = -1;
    }
    else if (s_state == PLAYER_PLAYING)
    {
        r = 0;
    }
    else if ((s_state == PLAYER_PAUSED) && (s_dec.opened != 0U))
    {
        player_prime_and_start();   /* re-prime: exact continuity */
        r = 0;
    }
    else
    {
        /* STOPPED with nothing loaded -> (re)load current index. */
        r = player_load_locked((uint32_t)s_cur, 1U);
    }
    PLOCK_GIVE();
    return r;
}

int player_pause(void)
{
    PLOCK_TAKE();
    if (s_state == PLAYER_PLAYING)
    {
        sai_audio_stop();
        sai_audio_drain();
        s_state = PLAYER_PAUSED;
    }
    PLOCK_GIVE();
    return 0;
}

int player_toggle(void)
{
    int r;
    PLOCK_TAKE();
    if (s_state == PLAYER_PLAYING)
    {
        r = player_pause();
    }
    else
    {
        r = player_play();
    }
    PLOCK_GIVE();
    return r;
}

int player_stop(void)
{
    PLOCK_TAKE();
    sai_audio_stop();
    sai_audio_drain();
    audio_decoder_close(&s_dec);
    s_state = PLAYER_STOPPED;
    PLOCK_GIVE();
    return 0;
}

int player_next(void)
{
    int r;
    uint8_t was_playing;
    uint32_t next;

    PLOCK_TAKE();
    if (s_count == 0U)
    {
        r = -1;
    }
    else
    {
        was_playing = (s_state == PLAYER_PLAYING) ? 1U : 0U;
        next = ((uint32_t)s_cur + 1U) % s_count;
        r = player_load_locked(next, was_playing);
    }
    PLOCK_GIVE();
    return r;
}

int player_prev(void)
{
    int r;
    uint8_t was_playing;
    uint32_t prev;

    PLOCK_TAKE();
    if (s_count == 0U)
    {
        r = -1;
    }
    else
    {
        was_playing = (s_state == PLAYER_PLAYING) ? 1U : 0U;
        prev = ((uint32_t)s_cur + s_count - 1U) % s_count;
        r = player_load_locked(prev, was_playing);
    }
    PLOCK_GIVE();
    return r;
}

int player_seek_percent(uint32_t pct)
{
    int r;
    PLOCK_TAKE();
    if (pct > 100U) { pct = 100U; }
    if (s_dec.opened == 0U)
    {
        r = -1;
    }
    else
    {
        r = audio_decoder_seek(&s_dec, pct);
    }
    PLOCK_GIVE();
    return r;
}

int player_set_volume(uint8_t v)
{
    if (v > 100U) { v = 100U; }
    s_vol = v;
    wm8978_set_volume(v);
    return 0;
}

uint8_t player_get_volume(void) { return s_vol; }

int player_current_index(void)  { return (int)s_cur; }

const char *player_current_title(void)
{
    if (s_count == 0U)
    {
        return "";
    }
    return s_tracks[s_cur].name;
}

uint32_t player_position_ms(void)
{
    return (s_dec.opened != 0U) ? s_dec.position_ms : 0U;
}

uint32_t player_duration_ms(void)
{
    return (s_dec.opened != 0U) ? s_dec.duration_ms : 0U;
}

uint32_t player_sample_rate(void)
{
    return (s_dec.opened != 0U) ? s_dec.sample_rate : 0U;
}
