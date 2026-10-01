/**
 ******************************************************************************
 * @file    ui_page_info.c
 * @brief   System info page - see ui_page_info.h.
 *
 *  Layout (480x360, origin top-left)
 *
 *      0   ┌─────────────────────────────────────────┐
 *          │            SYSTEM INFO                  │  36 px header
 *     36   ├─────────────────────────────────────────┤
 *          │  CPU                  Cortex-M85 @480MHz│
 *          │  Display           MIPI DSI 480x360 x2  │
 *          │  DSI             122 cmds / link 0x0110 │
 *          │  LVGL             fps 30 / flushes 4200 │
 *          │  Memory            31 KB / 128 KB (24%) │
 *          │  VSync                              1204│
 *    314   └─────────────────────────────────────────┘
 *
 *  Every label is created once; the 1 Hz timer only rewrites the text, so
 *  LVGL redraws just the dirty rectangles.
 ******************************************************************************
 */
#include "ui_page_info.h"
#include "ui_common.h"
#include "bsp_lcd.h"
#include "lv_port.h"

#include "bsp_api.h"

#define INFO_Y0 56
#define INFO_DY 46

typedef struct
{
    lv_obj_t *cpu;
    lv_obj_t *disp;
    lv_obj_t *dsi;
    lv_obj_t *lvgl;
    lv_obj_t *mem;
    lv_obj_t *vsync;
} info_handles_t;

static info_handles_t g_info = {0};

/** One "key ... value" row; returns the right-aligned value label. */
static lv_obj_t *info_row(lv_obj_t *scr, int32_t idx, const char *key)
{
    const int32_t y = INFO_Y0 + idx * INFO_DY;
    lv_obj_t *val;

    (void)ui_mk_label(scr, UI_PAD, y, UI_FONT(16), COL_LABEL, key);

    val = ui_mk_label(scr, 0, y, UI_FONT(16), COL_VALUE, "--");
    ui_align_right(val, y);

    return val;
}

static void ui_page_info_tick(lv_timer_t *timer)
{
    bsp_lcd_dsi_status_t ds;

    bsp_lcd_dsi_status(&ds);

    if (g_info.dsi != NULL)
    {
        lv_label_set_text_fmt(g_info.dsi, "%u cmds / link 0x%04x",
                              (unsigned int) ds.cmd_count,
                              (unsigned int) ds.link_status);
    }
    if (g_info.lvgl != NULL)
    {
        lv_label_set_text_fmt(g_info.lvgl, "fps %u / flushes %u",
                              (unsigned int) lv_port_fps(),
                              (unsigned int) lv_port_flush_count());
    }
    if (g_info.mem != NULL)
    {
        lv_label_set_text_fmt(g_info.mem, "%u KB / %u KB (%u%%)",
                              (unsigned int) lv_port_mem_used_kb(),
                              (unsigned int) LV_PORT_MEM_TOTAL_KB,
                              (unsigned int) lv_port_mem_used_pct());
    }
    if (g_info.vsync != NULL)
    {
        lv_label_set_text_fmt(g_info.vsync, "%u",
                              (unsigned int) bsp_lcd_vsync_count());
    }

    LV_UNUSED(timer);
}

lv_obj_t *ui_page_info_build(void)
{
    lv_obj_t *scr = ui_common_screen_create();

    (void)ui_common_header(scr, "SYSTEM INFO");

    g_info.cpu  = info_row(scr, 0, "CPU");
    g_info.disp = info_row(scr, 1, "Display");
    g_info.dsi  = info_row(scr, 2, "DSI");
    g_info.lvgl = info_row(scr, 3, "LVGL");
    g_info.mem  = info_row(scr, 4, "Memory");
    g_info.vsync = info_row(scr, 5, "VSync");

    /* Static values, written once. */
    lv_label_set_text_fmt(g_info.cpu, "Cortex-M85 @ %u MHz",
                          (unsigned int) (SystemCoreClock / 1000000U));
    lv_label_set_text_fmt(g_info.disp, "MIPI DSI %ux%u x%u",
                          (unsigned int) BSP_LCD_WIDTH,
                          (unsigned int) BSP_LCD_HEIGHT,
                          (unsigned int) BSP_LCD_FB_PAGES);

    ui_page_info_tick(NULL);
    (void)lv_timer_create(ui_page_info_tick, 1000, NULL);

    return scr;
}
