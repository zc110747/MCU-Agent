/**
 * @file    mjpeg_row.h
 * @brief   Hook between the vendored libjpeg row converter (third_party/jpeg) and this project's video surface.
 * @note    libjpeg 侧钩子, 升级 libjpeg 必须重放: document/stm32f429/video_player_port.md A3
 */
#ifndef MJPEG_ROW_H
#define MJPEG_ROW_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** RGB565 destination for one decoded scanline (at least `width` entries). */
uint16_t *mjpeg_row_buffer(void);

/** Hand one finished RGB565 scanline back to the surface owner. */
void mjpeg_row_emit(const uint16_t *row, uint32_t width);

/** Bump allocator used by libjpeg (jmemnobs.c) instead of malloc(). */
void *mjpeg_pool_alloc(size_t size);

#ifdef __cplusplus
}
#endif

#endif /* MJPEG_ROW_H */
