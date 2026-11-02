/**
 * @file    audio_pcm.h
 * @brief   PCM playback of AVI audio chunks through SAI1_A -> WM8978.
 * @note    document/stm32f429/video_player_port.md A4
 */
#ifndef AUDIO_PCM_H
#define AUDIO_PCM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bytes per DMA half.  ~46 ms of 44.1 kHz stereo PCM. */
#define AUDIO_PCM_HALF_BYTES 8192U

/** Longest single push, and the chunk size the AVI demuxer is read with. */
#define AUDIO_PCM_CHUNK_BYTES 4096U

/**
 * @brief  Configure the codec and the SAI for 16-bit PCM playback.
 * @param  sample_rate  stream sample rate in Hz
 * @param  channels     1 or 2
 * @retval 0 on success, 1 when the rate is unsupported or memory is missing
 */
int8_t audio_pcm_open(uint32_t sample_rate, uint16_t channels);

/** Append PCM bytes; blocks while both DMA halves are owned by the stream. */
int8_t audio_pcm_write(const uint8_t *data, uint32_t len);

/** Mute, stop the stream and free the buffers; safe to call twice. */
void audio_pcm_close(void);

/** 1 while the SAI is running. */
uint8_t audio_pcm_is_playing(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_PCM_H */
