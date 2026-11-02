/**
 * @file    video_player.h
 * @brief   MJPEG/AVI player task: scans 0:/VIDEO and plays every file in it.
 * @note    document/stm32f429/video_player_port.md A6
 */
#ifndef VIDEO_PLAYER_H
#define VIDEO_PLAYER_H

#ifdef __cplusplus
extern "C" {
#endif

/** Stack words for the decoder task (JPEG needs a few KB of locals). */
#define VIDEO_PLAYER_STACK_WORDS 1536U

/** Priority: above the UI pump, below nothing else in this project. */
#define VIDEO_PLAYER_PRIO 3U

/**
 * @brief  FreeRTOS task body.  Waits for the panel + LVGL + card, then plays
 *         the playlist forever (KEY0 next, KEY2 previous, KEY_UP forward,
 *         KEY1 backward).
 */
void video_player_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_PLAYER_H */
