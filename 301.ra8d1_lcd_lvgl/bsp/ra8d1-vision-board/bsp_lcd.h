/**
 * @file bsp_lcd.h
 * @brief 2.0" 480x360 MIPI DSI panel driven by the GLCDC through the DSI host
 *
 * Phase 5 replaces the Phase 2 parallel-RGB path.  The panel on this board is
 * a MIPI DSI unit, so the GLCDC's RGB565 output is bridged into the DSI host
 * (`phy_layer`) and serialised over 2 D-PHY lanes.  The panel additionally
 * needs a DCS power-on sequence, which ra8_mipi_lcd_init() pushes.
 *
 * Framebuffer: two full-screen pages in SDRAM, because LVGL runs in
 * LV_DISPLAY_RENDER_MODE_DIRECT and swaps pages with R_GLCDC_BufferChange().
 */
#ifndef BSP_LCD_H_
#define BSP_LCD_H_

#include "bsp_api.h"

#include <stdbool.h>
#include <stdint.h>

/** Visible panel geometry (landscape). Matches the DSI video timing. */
#define BSP_LCD_WIDTH      (480U)
#define BSP_LCD_HEIGHT     (360U)
#define BSP_LCD_BPP        (16U)

/* Row stride in pixels.  The generator formula is
 *   STRIDE_BYTES = ((W * BPP + 0x1FF) >> 9) << 6      (= 960 B for 480x16bpp)
 *   STRIDE_PIX   = STRIDE_BYTES * 8 / BPP             (= 480)
 * i.e. it is an identity at this resolution; the shape is kept so a future
 * geometry change cannot silently desynchronise the two. */
#define BSP_LCD_STRIDE     (BSP_LCD_WIDTH)
#define BSP_LCD_FB_BYTES   (BSP_LCD_STRIDE * BSP_LCD_HEIGHT * (BSP_LCD_BPP / 8U))

/** Number of DIRECT-mode framebuffer pages (double buffered). */
#define BSP_LCD_FB_PAGES   (2U)

/** Bring up SDRAM, the panel, the DSI host and the GLCDC. Safe to call once. */
fsp_err_t bsp_lcd_init (void);

/** Start the GLCDC scan-out (also called by bsp_lcd_init). */
fsp_err_t bsp_lcd_start (void);

/** Start of framebuffer page 0 (RGB565, in SDRAM). */
uint16_t * bsp_lcd_framebuffer (void);

/** Start of framebuffer page `index` (0 or 1). NULL if out of range. */
uint16_t * bsp_lcd_framebuffer_page (uint32_t index);

/** Point the GLCDC layer 0 at `buffer` on the next frame boundary.
 *  `buffer` must be one of the pages returned by bsp_lcd_framebuffer_page().
 *  Used by the LVGL DIRECT-mode flush to swap pages tear-free. */
fsp_err_t bsp_lcd_set_framebuffer (void * buffer);

void bsp_lcd_fill (uint16_t color);
void bsp_lcd_fill_rect (uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t color);
void bsp_lcd_draw_pixel (uint32_t x, uint32_t y, uint16_t color);
void bsp_lcd_set_backlight (bool on);

/** True once the GLCDC has been opened and started. */
bool bsp_lcd_ready (void);

/** Frames presented, counted from the GLCDC line-detect (vsync) interrupt. */
uint32_t bsp_lcd_vsync_count (void);

/** Called from the GLCDC line-detect ISR; forwards to the LVGL port so its
 *  flush can wait for the frame boundary.  Does nothing before LVGL starts. */
void bsp_lcd_vsync_notify (void);

/** GLCDC register snapshot, for the msh "lcd stat" command and tests. */
typedef struct
{
    uint32_t bg_en;        /* BG.EN:      bit0 EN, bit8 VEN, bit16 SWRST */
    uint32_t bg_hsize;     /* BG.HSIZE: full image horizontal size   */
    uint32_t bg_vsize;     /* BG.VSIZE: full image vertical size     */
    uint32_t stmon;        /* SYSCNT.STMON: bit0 VPOS, bit1 L1UNDF, bit2 L2UNDF */
    uint32_t panel_clk;    /* SYSCNT.PANEL_CLK: DCDR/CLKEN/CLKSEL/PIXSEL */
} bsp_lcd_status_t;

/** Read the GLCDC status registers. */
void bsp_lcd_status (bsp_lcd_status_t * p_status);

/** DSI link + PHY diagnostic snapshot, for the msh "lcd dsi" command. */
typedef struct
{
    uint32_t cmd_count;    /* DCS commands pushed by ra8_mipi_lcd_init() */
    uint32_t phy_status;   /* last PHY event word from the DSI callback  */
    uint32_t link_status;  /* R_MIPI_DSI_StatusGet().link_status         */
    uint32_t ack_err;      /* accumulated ack/error report               */
    uint32_t seq0_count;   /* SEQ0 (LP command done) interrupts seen     */
} bsp_lcd_dsi_status_t;

/** Read the DSI/PHY diagnostic registers. */
void bsp_lcd_dsi_status (bsp_lcd_dsi_status_t * p_status);

/** Demo test patterns, selectable from msh. */
void bsp_lcd_pattern (uint32_t index);
uint32_t bsp_lcd_pattern_count (void);

#endif /* BSP_LCD_H_ */
