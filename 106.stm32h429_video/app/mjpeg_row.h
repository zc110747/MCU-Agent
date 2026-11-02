/**
 ******************************************************************************
 * @file    mjpeg_row.h
 * @brief   Hook between the vendored libjpeg row converter (third_party/jpeg)
 *          and this project's video surface.
 *
 *          jdmerge.c no longer writes to the panel: it asks for a row buffer,
 *          fills it with RGB565 and hands the finished row back through
 *          mjpeg_row_emit().  All three functions are implemented in
 *          app/video_mjpeg.c.
 ******************************************************************************
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
