/**
 * @file bsp_lcd.h
 * @brief RGB 4.3" 800x480 panel driven straight by the GLCDC (no LVGL yet)
 *
 * Phase 2: one graphics layer, RGB565 framebuffer in SDRAM, no interrupts,
 * no callback. LVGL will sit on top of bsp_lcd_framebuffer() in Phase 4.
 */
#ifndef BSP_LCD_H_
#define BSP_LCD_H_

#include "bsp_api.h"

#include <stdbool.h>
#include <stdint.h>

#define BSP_LCD_WIDTH      (800U)
#define BSP_LCD_HEIGHT     (480U)
#define BSP_LCD_BPP        (16U)
#define BSP_LCD_STRIDE     (BSP_LCD_WIDTH)               /* pixels */
#define BSP_LCD_FB_BYTES   (BSP_LCD_STRIDE * BSP_LCD_HEIGHT * (BSP_LCD_BPP / 8U))

/** Bring up SDRAM, the panel and the GLCDC. Safe to call once. */
fsp_err_t bsp_lcd_init (void);

/** Start of the RGB565 framebuffer (in SDRAM). */
uint16_t * bsp_lcd_framebuffer (void);

void bsp_lcd_fill (uint16_t color);
void bsp_lcd_fill_rect (uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t color);
void bsp_lcd_draw_pixel (uint32_t x, uint32_t y, uint16_t color);
void bsp_lcd_set_backlight (bool on);

/** Raw GLCDC register snapshot, for the msh "lcd stat" command and tests. */
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

/** Demo test patterns, selectable from msh. */
void bsp_lcd_pattern (uint32_t index);
uint32_t bsp_lcd_pattern_count (void);

#endif /* BSP_LCD_H_ */
