/**
  ******************************************************************************
  * @file    audio_player.h
  * @brief   WAV playback engine: playlist walking, SAI double buffering, keys.
  *
  *          One task owns the whole audio path.  It blocks on a DMA
  *          transfer-complete semaphore and refills whichever half buffer just
  *          finished, so no FatFs call ever happens in interrupt context.
  *
  *          Pause is implemented by writing silence into the free buffer instead
  *          of stopping the DMA: the SAI/DAC clock chain keeps running, so
  *          resuming is instant and click-free.
  ******************************************************************************
  */
#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
  AUDIO_STATE_IDLE = 0,   /* nothing loaded, or card/playlist unavailable */
  AUDIO_STATE_PLAYING,
  AUDIO_STATE_PAUSED,
  AUDIO_STATE_ERROR,
} AUDIO_STATE_T;

typedef struct
{
  volatile AUDIO_STATE_T state;
  volatile int16_t  track;           /* index into the playlist, -1 = none */
  volatile uint32_t current_seconds;
  volatile uint32_t total_seconds;
  volatile uint16_t sample_rate;
  volatile uint16_t bits_per_sample;
  volatile uint16_t track_count;
  char name[96];                     /* file name of the current track */
} AUDIO_STATUS_T;

/**
  * @brief  Create the audio task.  Call once, after the SD card is mounted.
  *         Playback starts automatically with the first track.
  */
void audio_player_start(void);

/** Read-only snapshot for the UI task. */
const AUDIO_STATUS_T *audio_player_status(void);

/** KEY_UP: toggle between playing and paused. */
void audio_player_toggle_pause(void);

/** KEY0: jump to the next track (wraps). */
void audio_player_next(void);

/** KEY2: jump to the previous track (wraps). */
void audio_player_prev(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_PLAYER_H */
