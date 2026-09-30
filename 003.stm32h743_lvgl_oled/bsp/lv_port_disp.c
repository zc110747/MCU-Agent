/**
 ******************************************************************************
 * @file    lv_port_disp.c
 * @brief   LVGL display port for the ST7789 240x240 panel on SPI6.
 *
 *  Pixel format
 *  ------------
 *  LCD_CopyBuffer() reconfigures SPI6 to 16-bit frames and shifts them out MSB
 *  first, which lands on the wire exactly as the ST7789 expects RGB565.  That
 *  is why lv_conf.h keeps LV_COLOR_16_SWAP at 0 - no byte swapping anywhere.
 *
 *  Buffering
 *  ---------
 *  The transfer is a blocking, polled FIFO loop (no DMA), so a second draw
 *  buffer would buy nothing: LVGL cannot render ahead while the CPU is busy
 *  feeding the SPI.  One partial buffer of DISP_BUF_LINES rows is used instead.
 *  It lives in .bss which the linker script maps to AXI-SRAM.
 *
 *  Alignment
 *  ---------
 *  LCD_SPI_TransmitBuffer() pushes two pixels at a time through a 32-bit write
 *  to TXDR (`*(uint32_t *)pData`), so the buffer must be 4-byte aligned.  It is
 *  aligned to 32 here, which also keeps it on a cache line should D-cache ever
 *  be switched on.
 ******************************************************************************
 */
#include "lv_port_disp.h"
#include "lvgl.h"
#include "drv_spi_oled.h"

/* Rows kept in the draw buffer.  240 x 60 px x 2 B = 28.8 kB, i.e. a quarter of
 * the screen - well above the 1/10 LVGL asks for, and it keeps the number of
 * SPI transactions per full redraw down to four. */
#define DISP_BUF_LINES 60U

/* 显示端口状态：LVGL 绘制缓冲与显示驱动描述 */
typedef struct
{
    lv_disp_draw_buf_t draw_buf_dsc;
    lv_disp_drv_t      disp_drv;
    lv_color_t         draw_buf[LCD_Width * DISP_BUF_LINES] __attribute__((aligned(32)));
} disp_port_t;

static disp_port_t g_disp = {0};

/**
 * @brief  Push one rendered area to the panel.
 */
static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    uint16_t w = (uint16_t)(area->x2 - area->x1 + 1);
    uint16_t h = (uint16_t)(area->y2 - area->y1 + 1);

    LCD_CopyBuffer((uint16_t)area->x1, (uint16_t)area->y1, w, h, (uint16_t *)color_p);

    /* Blocking transfer: by the time we get here the pixels are already out. */
    lv_disp_flush_ready(drv);
}

void lv_port_disp_init(void)
{
    lv_disp_draw_buf_init(&g_disp.draw_buf_dsc, g_disp.draw_buf, NULL,
                          LCD_Width * DISP_BUF_LINES);

    lv_disp_drv_init(&g_disp.disp_drv);
    g_disp.disp_drv.hor_res  = LCD_Width;
    g_disp.disp_drv.ver_res  = LCD_Height;
    g_disp.disp_drv.flush_cb = disp_flush;
    g_disp.disp_drv.draw_buf = &g_disp.draw_buf_dsc;

    lv_disp_drv_register(&g_disp.disp_drv);
}
