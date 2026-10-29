/**
  ******************************************************************************
  * @file    lv_port_disp.c
  * @brief   LVGL display port for the NT35510 800x480 panel on FMC Bank1 NE1
  *          (8080 16-bit, polled writes -- no LTDC, no DMA2D).
  *
  *  Pixel format
  *  ------------
  *  LV_COLOR_DEPTH = 16 and LV_COLOR_16_SWAP = 0: one lv_color_t is one RGB565
  *  word, which is exactly what lcd_color_fill() writes into the panel GRAM.
  *  No byte swapping anywhere.
  *
  *  Buffering
  *  ---------
  *  The flush is a blocking FIFO write loop, so a second draw buffer would buy
  *  nothing -- LVGL cannot render ahead while the CPU is feeding the panel.
  *  One partial buffer of DISP_BUF_LINES rows is used instead: 800 x 60 x 2 B =
  *  96 kB, i.e. 12.5% of the screen, comfortably above the 10% LVGL asks for.
  *  It is taken from the FreeRTOS heap (external SDRAM) so the 192 kB of
  *  internal SRAM stays available for the audio buffers and task stacks.
  ******************************************************************************
  */
#include "lv_port_disp.h"
#include "lvgl.h"
#include "bsp_lcd.h"
#include "FreeRTOS.h"
#include <stdio.h>
#include "log.h"

/* Rows held in the draw buffer: 800 x 60 x 2 B = 96 kB. */
#define DISP_BUF_LINES      60U

static lv_disp_draw_buf_t   s_draw_buf_dsc;
static lv_disp_drv_t        s_disp_drv;
/* Draw buffer lives in SDRAM (pvPortMalloc) so an 800 px wide partial buffer
 * does not consume the 256 KB internal SRAM. */
static lv_color_t          *s_draw_buf = NULL;

/**
  * @brief  Push one rendered area to the panel.
  */
static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    uint16_t w = (uint16_t)(area->x2 - area->x1 + 1);
    uint16_t h = (uint16_t)(area->y2 - area->y1 + 1);

    lcd_color_fill((uint16_t)area->x1, (uint16_t)area->y1, w, h, (uint16_t *)color_p);

    /* Blocking transfer: by the time we get here the pixels are already out. */
    lv_disp_flush_ready(drv);
}

void lv_port_disp_init(void)
{
    s_draw_buf = (lv_color_t *)pvPortMalloc((size_t)LCD_Width *
                                            (size_t)DISP_BUF_LINES *
                                            sizeof(lv_color_t));
    if (s_draw_buf == NULL)
    {
        PRINT_LOG("[LVGL] draw buffer alloc FAILED (SDRAM heap?)\r\n");
        return;
    }

    lv_disp_draw_buf_init(&s_draw_buf_dsc, s_draw_buf, NULL,
                          (uint32_t)LCD_Width * DISP_BUF_LINES);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res  = LCD_Width;
    s_disp_drv.ver_res  = LCD_Height;
    s_disp_drv.flush_cb = disp_flush;
    s_disp_drv.draw_buf = &s_draw_buf_dsc;

    lv_disp_drv_register(&s_disp_drv);
}
