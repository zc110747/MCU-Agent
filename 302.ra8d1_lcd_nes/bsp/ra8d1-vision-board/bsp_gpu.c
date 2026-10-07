/**
 * @file bsp_gpu.c
 * @brief DRW2D drawing-engine wrapper (see bsp_gpu.h).
 *
 * The upstream NES project blitted through lcd_gpu_fill_array() in
 * libraries/HAL_Drivers/drv_lcd.c.  That file is an RT-Thread graphics-device
 * driver plus a PWM backlight path this board does not use, so the useful
 * dozen lines are reproduced here instead of dragging in the whole driver.
 *
 * Ordering that matters:
 *   1. bsp_lcd_init() must have run - d2_framebuffer() needs the GLCDC
 *      layer-0 buffer, and the DRW2D engine shares the AHB bus with it.
 *   2. d2_inithw() resets the engine; the renderbuffer pool is allocated from
 *      the RT-Thread heap (dave_memory.c calls d2_malloc -> its own allocator,
 *      not rt_malloc; see note below).
 *
 * Caching: BSP_CFG_DCACHE_ENABLED is 0 in this project, so the SDRAM
 * framebuffer is effectively coherent and no clean/invalidate is needed
 * around the blit.  That matches bsp_lcd.c's framebuffer placement rationale.
 *
 * dave2d's allocator: dave_memory.c has its own static pool when
 * DRW_CFG_CUSTOM_MALLOC is 0 (which is r_drw_cfg.h's setting here), so
 * d2_newrenderbuffer() does not touch the RT-Thread heap.
 */
#include "bsp_gpu.h"

#include <rtthread.h>

#include "bsp_lcd.h"
#include "dave_base.h"
#include "dave_driver.h"
#include "r_glcdc.h"

/* The 2D engine needs a few renderbuffers for its internal command lists.
   20x20 matches the upstream driver; the pool is a static array inside
   dave2d, so this is not a heap-pressure knob. */
#define BSP_GPU_RBUF_COUNT      (20U)
#define BSP_GPU_RBUF_SIZE       (20U)

/* d2_setblitsrc() documented limits.  Violating them corrupts the output
   rather than returning an error, so bsp_gpu_blit_surface() checks first. */
#define BSP_GPU_MAX_BLIT_W      (2048U)
#define BSP_GPU_MAX_BLIT_H      (1024U)
#define BSP_GPU_MAX_PITCH       (65536U)

static d2_device *        g_d2        = NULL;
static d2_renderbuffer *  g_rbuf      = NULL;
static d2_s32             g_d2_err    = D2_OK;
static volatile bool      g_gpu_ready = false;
static volatile uint32_t  g_blit_count = 0U;

fsp_err_t bsp_gpu_init (void)
{
    if (g_gpu_ready)
    {
        return FSP_SUCCESS;
    }

    if (!bsp_lcd_ready())
    {
        rt_kprintf("[gpu] bsp_lcd_init() must run first\n");
        return FSP_ERR_NOT_OPEN;
    }

    /* d2_opendevice(0): single device instance, no display controller. */
    g_d2 = d2_opendevice(0);
    if (NULL == g_d2)
    {
        rt_kprintf("[gpu] d2_opendevice failed\n");
        return FSP_ERR_NOT_OPEN;
    }

    /* d2_inithw() must not be called before the engine's clock is enabled;
       r_drw's module start is driven from bsp_clocks (MSTPCR), which the FSP
       BSP already handles at startup. */
    g_d2_err = d2_inithw(g_d2, 0);
    if (D2_OK != g_d2_err)
    {
        rt_kprintf("[gpu] d2_inithw failed: %ld\n", (long) g_d2_err);
        d2_closedevice(g_d2);
        g_d2 = NULL;
        return FSP_ERR_NOT_OPEN;
    }

    /* Declare the GLCDC layer-0 framebuffer as the render target.
       Pitch is in pixels for d2_framebuffer: BSP_LCD_STRIDE == BSP_LCD_WIDTH
       at this geometry (asserted in bsp_lcd.c). */
    d2_framebuffer(g_d2,
                   bsp_lcd_framebuffer(),
                   (d2_s32) BSP_LCD_STRIDE,
                   (d2_s32) BSP_LCD_WIDTH,
                   (d2_s32) BSP_LCD_HEIGHT,
                   (d2_u32) d2_mode_rgb565);

    /* Default state: opaque copy, no AA on axis-aligned blits. */
    d2_setblendmode(g_d2, d2_bm_alpha, d2_bm_one_minus_alpha);
    d2_setalphamode(g_d2, d2_am_constant);
    d2_setalpha(g_d2, UINT8_MAX);
    d2_setantialiasing(g_d2, 0);
    d2_setlinecap(g_d2, d2_lc_butt);
    d2_setlinejoin(g_d2, d2_lj_miter);

    g_rbuf = d2_newrenderbuffer(g_d2, BSP_GPU_RBUF_COUNT, BSP_GPU_RBUF_SIZE);
    if (NULL == g_rbuf)
    {
        rt_kprintf("[gpu] d2_newrenderbuffer failed\n");
        d2_closedevice(g_d2);
        g_d2 = NULL;
        return FSP_ERR_NOT_OPEN;
    }

    /* Start from a known screen so the first blit is not composited over
       whatever SDRAM held at power-up. */
    d2_selectrenderbuffer(g_d2, g_rbuf);
    d2_cliprect(g_d2, 0, 0, (d2_border) BSP_LCD_WIDTH, (d2_border) BSP_LCD_HEIGHT);
    d2_clear(g_d2, 0x000000);
    (void) d2_executerenderbuffer(g_d2, g_rbuf, 0);
    (void) d2_flushframe(g_d2);

    g_gpu_ready = true;
    rt_kprintf("[gpu] DRW2D up, target=0x%08x %ux%u stride=%u\n",
               (unsigned int) (uint32_t) bsp_lcd_framebuffer(),
               (unsigned int) BSP_LCD_WIDTH,
               (unsigned int) BSP_LCD_HEIGHT,
               (unsigned int) BSP_LCD_STRIDE);

    return FSP_SUCCESS;
}

bool bsp_gpu_ready (void)
{
    return g_gpu_ready;
}

fsp_err_t bsp_gpu_blit_array (uint32_t dst_x, uint32_t dst_y,
                              const uint16_t * src, uint32_t w, uint32_t h)
{
    /* Tightly packed source: pitch equals the window width. */
    return bsp_gpu_blit_surface(dst_x, dst_y, src, w, w, h);
}

fsp_err_t bsp_gpu_blit_surface (uint32_t dst_x, uint32_t dst_y,
                                const uint16_t * src, uint32_t src_pitch,
                                uint32_t w, uint32_t h)
{
    d2_s32 bheight;
    d2_s32 bwidth;

    if (!g_gpu_ready || (NULL == src) || (0U == w) || (0U == h))
    {
        return FSP_ERR_NOT_OPEN;
    }

    /* Clip: the engine's cliprect is the panel, but a wholly off-screen blit
       would still be a wasted command-list submission. */
    if ((dst_x >= BSP_LCD_WIDTH) || (dst_y >= BSP_LCD_HEIGHT))
    {
        return FSP_SUCCESS;
    }

    if ((dst_x + w) > BSP_LCD_WIDTH)
    {
        w = BSP_LCD_WIDTH - dst_x;
    }

    if ((dst_y + h) > BSP_LCD_HEIGHT)
    {
        h = BSP_LCD_HEIGHT - dst_y;
    }

    /* d2_setblitsrc is capped at 2048x1024 with pitch < 65536.  Exceeding it
       silently produces garbage, so refuse rather than corrupt the panel. */
    if ((w > BSP_GPU_MAX_BLIT_W) || (h > BSP_GPU_MAX_BLIT_H) ||
        (src_pitch >= BSP_GPU_MAX_PITCH))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    d2_selectrenderbuffer(g_d2, g_rbuf);
    d2_cliprect(g_d2, 0, 0, (d2_border) BSP_LCD_WIDTH, (d2_border) BSP_LCD_HEIGHT);

    /* Source pitch is in PIXELS, not bytes: d2_setblitsrc()'s own doc comment
       says "number of pixels (*not bytes*) per scanline".  Passing the surface
       width here (rather than the window width) is what makes a sub-rectangle
       blit land on the right rows. */
    d2_setblitsrc(g_d2, (void *) src, (d2_s32) src_pitch, (d2_s32) w, (d2_s32) h,
                  (d2_u32) d2_mode_rgb565);

    /* Scale is 1:1, so the blit destination extent is (w,h) in 4.4 fixed point
       at the top-left corner. */
    bwidth  = (d2_s32) (w << 4);
    bheight = (d2_s32) (h << 4);

    (void) d2_blitcopy(g_d2,
                       (d2_s32) w, (d2_s32) h,
                       0, 0,
                       (d2_width) bwidth, (d2_width) bheight,
                       (d2_point) (dst_x << 4), (d2_point) (dst_y << 4),
                       0);

    (void) d2_executerenderbuffer(g_d2, g_rbuf, 0);
    (void) d2_flushframe(g_d2);

    g_blit_count++;

    return FSP_SUCCESS;
}

fsp_err_t bsp_gpu_fill_rect (uint32_t dst_x, uint32_t dst_y,
                             uint32_t w, uint32_t h, uint16_t color)
{
    if (!g_gpu_ready)
    {
        return FSP_ERR_NOT_OPEN;
    }

    if ((dst_x >= BSP_LCD_WIDTH) || (dst_y >= BSP_LCD_HEIGHT))
    {
        return FSP_SUCCESS;
    }

    if ((dst_x + w) > BSP_LCD_WIDTH)
    {
        w = BSP_LCD_WIDTH - dst_x;
    }

    if ((dst_y + h) > BSP_LCD_HEIGHT)
    {
        h = BSP_LCD_HEIGHT - dst_y;
    }

    d2_selectrenderbuffer(g_d2, g_rbuf);
    d2_cliprect(g_d2, (d2_border) dst_x, (d2_border) dst_y,
                (d2_border) (dst_x + w), (d2_border) (dst_y + h));
    d2_setcolor(g_d2, 0, (d2_color) color);
    d2_renderbox(g_d2,
                 (d2_point) (dst_x << 4), (d2_point) (dst_y << 4),
                 (d2_width) (w << 4), (d2_width) (h << 4));
    (void) d2_executerenderbuffer(g_d2, g_rbuf, 0);
    (void) d2_flushframe(g_d2);

    /* Renderbuffers can carry a clip rectangle; reset it so the next blit
       starts from the full panel again. */
    d2_cliprect(g_d2, 0, 0, (d2_border) BSP_LCD_WIDTH, (d2_border) BSP_LCD_HEIGHT);

    return FSP_SUCCESS;
}

int32_t bsp_gpu_last_error (void)
{
    return (int32_t) g_d2_err;
}

uint32_t bsp_gpu_blit_count (void)
{
    return g_blit_count;
}
