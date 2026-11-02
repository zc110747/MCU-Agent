/**
 * @file    lv_port_disp.c
 * @brief   LVGL display port for the 800x480 NT35510 panel on FMC Bank1 NE1.
 * @note    缓冲区与阻塞推屏: document/stm32f429/video_player_port.md A2
 */
#include "lv_port_disp.h"
#include "lvgl.h"
#include "bsp_lcd.h"
#include "FreeRTOS.h"
#include <stdio.h>
#include "log.h"

/* Rows per draw buffer: 60 = 1/8 of the 480-line panel (96 000 B at 800 px),
 * well above the 1/10 LVGL asks for. */
#define DISP_BUF_LINES 60U

/* The LVGL display: driver, its draw-buffer descriptor and the buffer itself. */
typedef struct
{
    lv_disp_draw_buf_t dsc;
    lv_disp_drv_t      drv;
    /* In SDRAM (pvPortMalloc): a full-width partial buffer would eat the 256 KB
     * internal SRAM. */
    lv_color_t *buf;
} DISP_STATE_T;

static DISP_STATE_T s_disp = {0};

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
    s_disp.buf = (lv_color_t *)pvPortMalloc((size_t)LCD_Width *
                                            (size_t)DISP_BUF_LINES *
                                            sizeof(lv_color_t));
    if (s_disp.buf == NULL)
    {
        PRINT_LOG("[LVGL] draw buffer alloc FAILED (SDRAM heap?)\r\n");
        return;
    }

    lv_disp_draw_buf_init(&s_disp.dsc, s_disp.buf, NULL,
                          (uint32_t)LCD_Width * DISP_BUF_LINES);

    lv_disp_drv_init(&s_disp.drv);
    s_disp.drv.hor_res  = LCD_Width;
    s_disp.drv.ver_res  = LCD_Height;
    s_disp.drv.flush_cb = disp_flush;
    s_disp.drv.draw_buf = &s_disp.dsc;

    lv_disp_drv_register(&s_disp.drv);
}
