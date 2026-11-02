/**
 * @file    video_ui.h
 * @brief   LVGL screen for the MJPEG player: one full-screen true-colour image as the video surface, plus three text bands on top of it.
 * @note    document/stm32f429/video_player_port.md A2
 */
#ifndef VIDEO_UI_H
#define VIDEO_UI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Video rectangle origin: the two text bands at the top stay uncovered. */
#define VIDEO_UI_TOP_BAND 76U

/**
 * @brief  Create the screen and allocate the RGB565 surface (SDRAM).
 *         Requires lv_init() + lv_port_disp_init() + the GBK fonts.
 * @retval 0 on success, 1 when the surface could not be allocated
 */
int8_t video_ui_init(void);

/** 1 once the screen exists and the surface is usable. */
uint8_t video_ui_ready(void);

/** RGB565 surface written by the decoder (full panel, LVGL owns nothing else). */
uint16_t *video_ui_surface(void);

/** Panel size of that surface. */
void video_ui_surface_size(uint16_t *width, uint16_t *height);

/** Title band: file name plus its index in the playlist. */
void video_ui_set_title(const char *name, uint16_t index, uint16_t total);

/** Second band: playback time / total, and the measured frame rate. */
void video_ui_set_status(const char *text);

/** Push the freshly decoded surface to the panel. */
void video_ui_frame_ready(void);

/** Serialise access to LVGL from any task. */
void video_ui_lock(void);
void video_ui_unlock(void);

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_UI_H */
