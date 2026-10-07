/**
 * @file bsp_gpu.h
 * @brief DRW2D (D/AVE 2D, TES Dave2D) drawing engine wrapper.
 *
 * The panel is driven by the GLCDC, whose layer-0 framebuffer lives in SDRAM.
 * Blitting a CPU-composed image into that buffer is the one operation this
 * project needs, and the RA8D1 has a dedicated 2D engine for it:
 *
 *   d2_opendevice / d2_inithw    - take the DRW2D hardware (r_drw FSP module)
 *   d2_framebuffer               - declare the GLCDC layer-0 buffer as target
 *   d2_blitcopy                  - the actual copy
 *   d2_flushframe                - wait for completion
 *
 * This is what the upstream NES project did through lcd_gpu_fill_array();
 * the wrapper is kept small and named for this project's BSP.
 */
#ifndef BSP_GPU_H_
#define BSP_GPU_H_

#include "bsp_api.h"

#include <stdbool.h>
#include <stdint.h>

/** Bring up the DRW2D engine and point it at the GLCDC layer-0 framebuffer.
 *  Safe to call more than once. Returns FSP_SUCCESS / an FSP error code. */
fsp_err_t bsp_gpu_init (void);

/** True once bsp_gpu_init() has succeeded. */
bool bsp_gpu_ready (void);

/** Copy a tightly packed RGB565 source rectangle into the framebuffer.
 *
 *  `src` is `w` pixels wide, `h` pixels tall, no padding.  `dst_x`/`dst_y` are
 *  in framebuffer pixels and are clipped to the panel.  Blocks until the
 *  engine reports the frame done.
 *
 *  Returns FSP_SUCCESS, or FSP_ERR_NOT_OPEN if the engine is not up. */
fsp_err_t bsp_gpu_blit_array (uint32_t dst_x, uint32_t dst_y,
                              const uint16_t * src, uint32_t w, uint32_t h);

/** Copy a window out of a larger RGB565 surface into the framebuffer.
 *
 *  Difference from bsp_gpu_blit_array(): the source is a *sub-rectangle* of a
 *  bigger image.  `src_pitch` is the surface's full width in PIXELS (not
 *  bytes) - Dave2D's d2_setblitsrc() documents its pitch argument as "number of
 *  pixels per scanline", so a window out of a 240-wide NES frame passes 240.
 *  The window is `w` x `h` and starts at the surface origin.
 *
 *  Why this matters: it lets a whole 240x240 frame go out as ONE engine
 *  submission.  Emitting it as 240 single-scanline blits measured at ~10.2 M
 *  cycles per frame (127% of the 60 Hz budget) because every call re-arms the
 *  engine; one submission is two orders of magnitude cheaper. */
fsp_err_t bsp_gpu_blit_surface (uint32_t dst_x, uint32_t dst_y,
                                const uint16_t * src, uint32_t src_pitch,
                                uint32_t w, uint32_t h);

/** Fill a framebuffer rectangle with one RGB565 colour, through the engine. */
fsp_err_t bsp_gpu_fill_rect (uint32_t dst_x, uint32_t dst_y,
                             uint32_t w, uint32_t h, uint16_t color);

/** Last engine error code (d2_s32 as returned by d2_inithw), for diagnostics. */
int32_t bsp_gpu_last_error (void);

/** Completed blits, counted by this wrapper. */
uint32_t bsp_gpu_blit_count (void);

#endif /* BSP_GPU_H_ */
