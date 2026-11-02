/**
 * @file    video_mjpeg.h
 * @brief   MJPEG frame decoder: one JPEG frame -> one rectangle of an RGB565 surface (the LVGL video image lives in SDRAM).
 * @note    document/stm32f429/video_player_port.md A3
 */
#ifndef VIDEO_MJPEG_H
#define VIDEO_MJPEG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Bind the decoder to a destination surface and allocate its working
 *         memory (decompressor state + a 38 KB libjpeg pool).
 *
 * @param  surface    RGB565 buffer, surface_w x surface_h pixels
 * @param  surface_w  pixels per row (also the decoder's stride)
 * @param  surface_h  rows in the surface
 * @param  off_x      left edge of the video rectangle inside the surface
 * @param  off_y      top edge of the video rectangle inside the surface
 *
 * @retval 0 on success, 1 when the pool or a row buffer could not be allocated
 */
int8_t video_mjpeg_open(uint16_t *surface, uint16_t surface_w,
                        uint16_t surface_h, uint16_t off_x, uint16_t off_y);

/** Release the working memory; safe to call twice. */
void video_mjpeg_close(void);

/**
 * @brief  Decode one JPEG frame into the surface.
 * @retval 0 ok, 1 empty payload, 2 libjpeg error (frame skipped)
 */
int8_t video_mjpeg_decode(const uint8_t *data, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_MJPEG_H */
