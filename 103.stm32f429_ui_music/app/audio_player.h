/**
  ******************************************************************************
  * @file    app/audio_player.h
  * @brief   Music player engine: track list, transport state machine and the
  *         SAI double-buffer refill loop.
  *
  *  The SAI DMA double buffer IS the playback ring: a FreeRTOS task blocks on
  *  sai_audio_get_empty_half() and decodes the next batch of 16-bit stereo PCM
  *  straight into the half that just finished playing.  No extra ring buffer
  *  is needed.  Volume rides on the WM8978 digital DAC volume; the UI volume
  *  bar is a mirror of player_get_volume().
  ******************************************************************************
  */
#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <stdint.h>

typedef enum
{
    PLAYER_STOPPED = 0,
    PLAYER_PLAYING,
    PLAYER_PAUSED,
    PLAYER_PRIMING   /* a prime (decode + DMA start) has been requested and is
                       being performed by the refill task, NOT the UI task */
} player_state_t;

typedef struct
{
    char path[288];   /* full FatFs path (vol:/music/<name>, <=255+slash) */
    char name[64];    /* display name (file name, no directory)   */
} track_t;

#define PLAYER_MAX_TRACKS   64U

/**
  * @brief  Power up WM8978 + SAI and spawn the refill task.  Call AFTER the
  *         FreeRTOS scheduler is running and the SDRAM heap exists.
  * @retval 0 ok, -1 fail
  */
int player_init(void);

/**
  * @brief  Scan "vol:/music" for .wav/.mp3 files (vol = "0:" or "1:").
  * @retval number of tracks found (0 if the directory is missing)
  */
uint32_t player_scan(const char *vol);

uint32_t       player_count(void);
player_state_t player_state(void);

int  player_play(void);   /* start or resume the current track       */
int  player_load(uint32_t idx, uint8_t autoplay); /* load track idx; autoplay!=0 starts playing */
int  player_pause(void);
int  player_toggle(void);  /* play<->pause on the same track          */
int  player_stop(void);
int  player_next(void);    /* load+play next (preserves playing state)*/
int  player_prev(void);    /* load+play prev (preserves playing state)*/
int  player_seek_percent(uint32_t pct);

int  player_set_volume(uint8_t v);   /* 0..100 */
uint8_t player_get_volume(void);

int     player_current_index(void);
const char *player_current_title(void);
uint32_t player_position_ms(void);
uint32_t player_duration_ms(void);
uint32_t player_sample_rate(void);

#endif /* AUDIO_PLAYER_H */
