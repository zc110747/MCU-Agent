/**
 * @file    audio_pcm.c
 * @brief   PCM playback through SAI1_A + WM8978 with two DMA halves.
 * @note    SAI 双缓冲与 prefill 顺序: document/stm32f429/video_player_port.md A4
 */
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "audio_pcm.h"
#include "bsp_sai.h"
#include "bsp_wm8978.h"
#include "log.h"

/* Audio stream state, grouped so open/close/snapshot touch one object. */
typedef struct
{
    uint8_t          *halves[2];
    uint8_t          *stage;
    uint32_t          stage_fill;
    uint32_t          sample_rate;
    uint16_t          channels;
    uint8_t           prefill; /* 0/1 = next half to prefill, 2 = stream handed to DMA */
    uint8_t           playing;
    SemaphoreHandle_t half_free;
} audio_state_t;

static audio_state_t s_audio = {0};

static void audio_dma_complete(void)
{
    BaseType_t woken = pdFALSE;

    if (s_audio.half_free != NULL)
    {
        (void)xSemaphoreGiveFromISR(s_audio.half_free, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

/** Hand the staging buffer to the DMA: prefill first, then per completion. */
static int8_t audio_push_half(void)
{
    uint8_t index;

    if (s_audio.prefill < 2U)
    {
        index = s_audio.prefill;
        s_audio.prefill++;
    }
    else
    {
        /* Wait for the stream to hand a half back.  A timeout means the
         * consumer stalled; drop the half rather than block the decoder. */
        if (xSemaphoreTake(s_audio.half_free, pdMS_TO_TICKS(500)) != pdTRUE)
        {
            PRINT_LOG("[AUD ] refill timeout, half dropped\r\n");
            return 1;
        }
        index = bsp_sai_free_buffer();
    }

    memcpy(s_audio.halves[index], s_audio.stage, AUDIO_PCM_HALF_BYTES);

    if ((s_audio.prefill == 2U) && (s_audio.playing == 0U))
    {
        bsp_sai_play();
        s_audio.playing = 1U;
        /* Open the analog path only once real data is clocking out. */
        bsp_wm8978_unmute();
    }

    return 0;
}

int8_t audio_pcm_open(uint32_t sample_rate, uint16_t channels)
{
    uint16_t channels_used = channels;

    if ((sample_rate < 8000U) || (sample_rate > 96000U))
    {
        PRINT_LOG("[AUD ] %lu Hz not supported\r\n", (unsigned long)sample_rate);
        return 1;
    }
    if ((channels_used != 1U) && (channels_used != 2U))
    {
        channels_used = 2U;
    }

    audio_pcm_close();

    s_audio.halves[0] = (uint8_t *)pvPortMalloc(AUDIO_PCM_HALF_BYTES);
    s_audio.halves[1] = (uint8_t *)pvPortMalloc(AUDIO_PCM_HALF_BYTES);
    s_audio.stage     = (uint8_t *)pvPortMalloc(AUDIO_PCM_HALF_BYTES);
    s_audio.half_free = xSemaphoreCreateBinary();

    if ((s_audio.halves[0] == NULL) || (s_audio.halves[1] == NULL) ||
        (s_audio.stage == NULL) || (s_audio.half_free == NULL))
    {
        PRINT_LOG("[AUD ] out of memory (SDRAM heap)\r\n");
        audio_pcm_close();
        return 1;
    }

    memset(s_audio.halves[0], 0, AUDIO_PCM_HALF_BYTES);
    memset(s_audio.halves[1], 0, AUDIO_PCM_HALF_BYTES);
    s_audio.stage_fill  = 0U;
    s_audio.prefill     = 0U;
    s_audio.playing     = 0U;
    s_audio.sample_rate = sample_rate;
    s_audio.channels    = channels_used;

    bsp_sai_set_dma_callback(audio_dma_complete);

    bsp_wm8978_mute();
    bsp_wm8978_set_i2s_format(WM8978_FMT_I2S, WM8978_WLEN_16);
    bsp_wm8978_set_dac(1U);
    bsp_wm8978_set_output(1U);
    bsp_wm8978_set_hp_volume(40U, 40U);
    bsp_wm8978_set_spk_volume(50U);

    if (!bsp_sai_configure(BSP_SAI_BITS_16, sample_rate, s_audio.halves[0],
                           s_audio.halves[1], (uint16_t)AUDIO_PCM_HALF_BYTES))
    {
        PRINT_LOG("[AUD ] SAI configure FAILED\r\n");
        audio_pcm_close();
        return 1;
    }

    PRINT_LOG("[AUD ] %lu Hz, %u ch, 16-bit\r\n",
              (unsigned long)sample_rate, (unsigned)channels_used);
    return 0;
}

int8_t audio_pcm_write(const uint8_t *data, uint32_t len)
{
    uint32_t offset = 0U;

    if (s_audio.stage == NULL)
    {
        return 1;
    }

    while (offset < len)
    {
        uint32_t room = AUDIO_PCM_HALF_BYTES - s_audio.stage_fill;
        uint32_t take = len - offset;

        if (take > room)
        {
            take = room;
        }

        memcpy(&s_audio.stage[s_audio.stage_fill], data + offset, take);
        s_audio.stage_fill += take;
        offset += take;

        if (s_audio.stage_fill >= AUDIO_PCM_HALF_BYTES)
        {
            if (audio_push_half() != 0)
            {
                s_audio.stage_fill = 0U;
                return 1;
            }
            s_audio.stage_fill = 0U;
        }
    }

    return 0;
}

void audio_pcm_close(void)
{
    if (s_audio.playing != 0U)
    {
        bsp_wm8978_mute();
        bsp_sai_stop();
        s_audio.playing = 0U;
    }

    if (s_audio.halves[0] != NULL)
    {
        vPortFree(s_audio.halves[0]);
        s_audio.halves[0] = NULL;
    }
    if (s_audio.halves[1] != NULL)
    {
        vPortFree(s_audio.halves[1]);
        s_audio.halves[1] = NULL;
    }
    if (s_audio.stage != NULL)
    {
        vPortFree(s_audio.stage);
        s_audio.stage = NULL;
    }
    if (s_audio.half_free != NULL)
    {
        vSemaphoreDelete(s_audio.half_free);
        s_audio.half_free = NULL;
    }

    s_audio.stage_fill = 0U;
    s_audio.prefill    = 0U;
}

uint8_t audio_pcm_is_playing(void)
{
    return s_audio.playing;
}
