/**
 ******************************************************************************
 * @file    ui_page_main.c
 * @brief   The one and only screen: board identity + live hardware telemetry
 *          + a real-time clock driven by the hardware RTC.
 *
 *  Layout (480x360, origin top-left)
 *
 *      0   ┌─────────────────────────────────────────┐
 *          │           RA8D1 VISION BOARD            │  36 px header
 *     36   ├─────────────────────────────────────────┤
 *          │              2026-01-01  Thu            │  date     (14 px)
 *     62   │              00:00:00                   │  clock    (28 px, cyan)
 *    104   ├─────────────────────────────────────────┤
 *          │  CPU                  Cortex-M85 @480MHz│
 *          │  Display           MIPI DSI 480x360 x2  │
 *          │  DSI             122 cmds / link 0x0110 │
 *          │  LVGL             fps 30 / flushes 4200 │
 *          │  Memory            31 KB / 128 KB (24%) │
 *          │  Uptime                       132 s      │
 *    330   └─────────────────────────────────────────┘
 *
 *  The clock is the point of the screen: bsp_rtc keeps calendar time on the
 *  32.768 kHz sub-clock, so this shows real time, not a reset-relative
 *  counter.  The 1 Hz timer only rewrites label text - LVGL then repaints
 *  just the changed rectangles, so the panel is not redrawn wholesale.
 ******************************************************************************
 */
#include "ui_page_main.h"
#include "ui_common.h"
#include "bsp_lcd.h"
#include "bsp_rtc.h"
#include "lv_port.h"

#include "bsp_api.h"

#include <rtthread.h>

#define MAIN_CLOCK_Y   56
#define MAIN_INFO_Y0   128
#define MAIN_INFO_DY   40

static const char *const k_wday[7] =
{
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

typedef struct
{
    lv_obj_t *date;
    lv_obj_t *clock;
    lv_obj_t *cpu;
    lv_obj_t *disp;
    lv_obj_t *dsi;
    lv_obj_t *lvgl;
    lv_obj_t *mem;
    lv_obj_t *uptime;
} main_handles_t;

static main_handles_t g_main;

/** One "key ....... value" row; returns the right-aligned value label. */
static lv_obj_t *main_row(lv_obj_t *scr, int32_t idx, const char *key)
{
    const int32_t y = MAIN_INFO_Y0 + idx * MAIN_INFO_DY;
    lv_obj_t *val;

    (void)ui_mk_label(scr, UI_PAD, y, UI_FONT(16), COL_LABEL, key);

    val = ui_mk_label(scr, 0, y, UI_FONT(16), COL_VALUE, "--");
    ui_align_right(val, y);

    return val;
}

static void ui_page_main_tick(lv_timer_t *timer)
{
    bsp_rtc_time_t t;
    bsp_lcd_dsi_status_t ds;

    /* --- live clock + date (hardware RTC) --- */
    if (bsp_rtc_get(&t))
    {
        if (g_main.clock != NULL)
        {
            lv_label_set_text_fmt(g_main.clock, "%02u:%02u:%02u",
                                  (unsigned int) t.hour,
                                  (unsigned int) t.min,
                                  (unsigned int) t.sec);
        }
        if (g_main.date != NULL)
        {
            lv_label_set_text_fmt(g_main.date, "%04u-%02u-%02u  %s",
                                  (unsigned int) t.year,
                                  (unsigned int) t.mon,
                                  (unsigned int) t.mday,
                                  k_wday[t.wday & 0x07U]);
        }
    }

    /* --- telemetry --- */
    bsp_lcd_dsi_status(&ds);

    if (g_main.dsi != NULL)
    {
        lv_label_set_text_fmt(g_main.dsi, "%u cmds / link 0x%04x",
                              (unsigned int) ds.cmd_count,
                              (unsigned int) ds.link_status);
    }
    if (g_main.lvgl != NULL)
    {
        lv_label_set_text_fmt(g_main.lvgl, "fps %u / flushes %u",
                              (unsigned int) lv_port_fps(),
                              (unsigned int) lv_port_flush_count());
    }
    if (g_main.mem != NULL)
    {
        lv_label_set_text_fmt(g_main.mem, "%u KB / %u KB (%u%%)",
                              (unsigned int) lv_port_mem_used_kb(),
                              (unsigned int) LV_PORT_MEM_TOTAL_KB,
                              (unsigned int) lv_port_mem_used_pct());
    }
    if (g_main.uptime != NULL)
    {
        lv_label_set_text_fmt(g_main.uptime, "%u s",
                              (unsigned int) (rt_tick_get() / RT_TICK_PER_SECOND));
    }

    LV_UNUSED(timer);
}

lv_obj_t *ui_page_main_build(void)
{
    lv_obj_t *scr = ui_common_screen_create();

    (void)ui_common_header(scr, "RA8D1 VISION BOARD");

    /* date + big clock, centred on the panel */
    g_main.date  = ui_mk_label_center(scr, MAIN_CLOCK_Y, UI_FONT(14),
                                      COL_DATE, "---- -- --");
    g_main.clock = ui_mk_label_center(scr, MAIN_CLOCK_Y + 24, UI_FONT(28),
                                      COL_CLOCK, "--:--:--");

    (void)ui_mk_separator(scr, MAIN_INFO_Y0 - 12);

    /* hardware telemetry rows */
    g_main.cpu    = main_row(scr, 0, "CPU");
    g_main.disp   = main_row(scr, 1, "Display");
    g_main.dsi    = main_row(scr, 2, "DSI");
    g_main.lvgl   = main_row(scr, 3, "LVGL");
    g_main.mem    = main_row(scr, 4, "Memory");
    g_main.uptime = main_row(scr, 5, "Uptime");

    /* Static values, written once. */
    lv_label_set_text_fmt(g_main.cpu, "Cortex-M85 @ %u MHz",
                          (unsigned int) (SystemCoreClock / 1000000U));
    lv_label_set_text_fmt(g_main.disp, "MIPI DSI %ux%u x%u",
                          (unsigned int) BSP_LCD_WIDTH,
                          (unsigned int) BSP_LCD_HEIGHT,
                          (unsigned int) BSP_LCD_FB_PAGES);

    ui_page_main_tick(NULL);
    (void)lv_timer_create(ui_page_main_tick, 1000, NULL);

    return scr;
}
