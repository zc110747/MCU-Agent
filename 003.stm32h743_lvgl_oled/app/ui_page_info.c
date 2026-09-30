/**
 ******************************************************************************
 * @file    ui_page_info.c
 * @brief   Main info panel - see ui_page_info.h.
 *
 *  Layout (all coordinates in pixels, origin top-left)
 *
 *      0   ┌───────────────────────────────┐
 *          │  STM32H743 信息面板           │  28 px header
 *     28   ├───────────────────────────────┤
 *          │          20:34:48             │  32 px clock
 *          │     2026-08-05  星期三        │  16 px date
 *     98   ├───────────────────────────────┤
 *          │  SD卡容量            FAT32    │
 *          │  可用 12.3 GB / 29.7 GB       │
 *          │  ▓▓▓▓▓▓▓░░░░░░░░░░░░░  58%    │
 *    156   ├───────────────────────────────┤
 *          │  主频  480 MHz         HSE    │
 *          │  运行  00:12:34               │
 *          │  字库  鸿蒙TTF         LSE     │  16 px
 *          │  缓存  命中 512 / 读卡 96     │  12 px
 *    240   └───────────────────────────────┘
 *
 *  Every label is created once; the refresh timer only rewrites the text, so
 *  LVGL redraws just the dirty rectangles and the SPI traffic stays low.
 ******************************************************************************
 */
#include "ui_page_info.h"
#include "ui_common.h"
#include "lvgl.h"
#include "main.h"
#include "drv_rtc.h"
#include "drv_sdio.h"
#include "drv_oled_text.h"
#include "lv_font_provider.h"
#include "lv_font_harmony.h"
#include "lv_font_gbk.h"
#include "lvgl_font.h"
#include <stdio.h>

/* SD capacity is re-read every N refresh ticks (tick = 1 s). */
#define SD_REFRESH_PERIOD 30U

/* Page-specific geometry (shared geometry/palette lives in ui_common.h). */
#define CLOCK_Y 34
#define DATE_Y 74
#define SEP1_Y 98
#define SD_HEAD_Y 104
#define SD_VAL_Y 124
#define SD_BAR_Y 146
#define SD_BAR_H 8
#define SEP2_Y 160
#define INFO1_Y 166
#define INFO2_Y 186
#define INFO3_Y 206
#define INFO4_Y 226

typedef struct
{
    lv_obj_t *clock;
    lv_obj_t *date;
    lv_obj_t *sd_head;
    lv_obj_t *sd_fs;
    lv_obj_t *sd_val;
    lv_obj_t *sd_bar;
    lv_obj_t *sd_pct;
    lv_obj_t *freq;
    lv_obj_t *clksrc;
    lv_obj_t *uptime;
    lv_obj_t *fontinfo;
    lv_obj_t *cache;
} ui_handles_t;

/* 信息页状态：控件句柄 + 刷新节拍 */
typedef struct
{
    ui_handles_t ui;
    uint32_t     sd_countdown; /* 0 -> query on the next tick */
    uint32_t     uptime_sec;
} info_page_t;

static info_page_t g_info = {0};

/*----------------------------------------------------------------------------*/
/* Data refresh                                                               */
/*----------------------------------------------------------------------------*/

static void refresh_clock(void)
{
    rtc_datetime_t dt;

    if (drv_rtc_get(&dt) != RT_OK)
    {
        lv_label_set_text(g_info.ui.clock, "--:--:--");
        lv_label_set_text(g_info.ui.date, "RTC 未启动");
        return;
    }

    lv_label_set_text_fmt(g_info.ui.clock, "%02d:%02d:%02d",
                          (int)dt.hour, (int)dt.minute, (int)dt.second);

    lv_label_set_text_fmt(g_info.ui.date, "%04d-%02d-%02d  %s",
                          (int)dt.year, (int)dt.month, (int)dt.day,
                          drv_rtc_weekday_cn(dt.weekday));
}

static void refresh_sd(void)
{
    sd_info_t info;
    char      used_str[16];
    char      total_str[16];
    uint32_t  pct;

    if (drv_sd_query_info(&info) != RT_OK)
    {
        lv_label_set_text(g_info.ui.sd_val, "读取失败");
        lv_obj_set_style_text_color(g_info.ui.sd_val, lv_color_hex(COL_ERR),
                                    LV_PART_MAIN);
        lv_label_set_text(g_info.ui.sd_fs, "--");
        lv_bar_set_value(g_info.ui.sd_bar, 0, LV_ANIM_OFF);
        lv_label_set_text(g_info.ui.sd_pct, "--%");
        return;
    }

    lv_obj_set_style_text_color(g_info.ui.sd_val, lv_color_hex(COL_VALUE),
                                LV_PART_MAIN);

    drv_sd_format_size(info.fs_total_bytes - info.fs_free_bytes,
                       used_str, sizeof(used_str));
    drv_sd_format_size(info.fs_total_bytes, total_str, sizeof(total_str));

    /* Scale down before the division so a 2 TB card cannot overflow. */
    pct = 0U;
    if (info.fs_total_bytes != 0U)
    {
        pct = (uint32_t)(((info.fs_total_bytes - info.fs_free_bytes) / 1024U) *
                         100U / (info.fs_total_bytes / 1024U));
        if (pct > 100U)
        {
            pct = 100U;
        }
    }

    lv_label_set_text_fmt(g_info.ui.sd_val, "已用 %s / %s", used_str, total_str);
    lv_label_set_text_fmt(g_info.ui.sd_fs, "%s %s",
                          drv_sd_card_name(info.card_type),
                          drv_sd_fs_name(info.fs_type));
    lv_bar_set_value(g_info.ui.sd_bar, (int32_t)pct, LV_ANIM_OFF);
    lv_label_set_text_fmt(g_info.ui.sd_pct, "%d%%", (int)pct);
}

static void refresh_runtime(void)
{
    uint32_t hits = 0U;
    uint32_t miss = 0U;

    lv_label_set_text_fmt(g_info.ui.uptime, "运行  %02d:%02d:%02d",
                          (int)(g_info.uptime_sec / 3600U),
                          (int)((g_info.uptime_sec / 60U) % 60U),
                          (int)(g_info.uptime_sec % 60U));

    /* Show the cache counters of whichever engine is actually live. */
    switch (lv_font_provider_engine())
    {
    case FONT_ENGINE_HARMONYOS:
        lv_font_harmony_stats(&hits, &miss);
        break;
    case FONT_ENGINE_CTF: {
        lvgl_font_stats_t st;
        lvgl_font_get_stats(&st);
        hits = st.bmp_hits;
        miss = st.bmp_misses;
        break;
    }
    default:
        lv_font_gbk_cache_stats(&hits, &miss);
        break;
    }

    lv_label_set_text_fmt(g_info.ui.cache, "缓存  命中 %lu / 读卡 %lu",
                          (unsigned long)hits, (unsigned long)miss);
}

/**
 * @brief  1 Hz refresh timer.
 */
static void ui_page_info_tick(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    g_info.uptime_sec++;

    refresh_clock();
    refresh_runtime();

    if (g_info.sd_countdown == 0U)
    {
        refresh_sd();
        g_info.sd_countdown = SD_REFRESH_PERIOD;
    }
    else
    {
        g_info.sd_countdown--;
    }
}

/*----------------------------------------------------------------------------*/
/* Build                                                                      */
/*----------------------------------------------------------------------------*/

lv_obj_t *ui_page_info_build(void)
{
    lv_obj_t *scr = ui_common_screen_create();
    lv_obj_t *hdr;

    hdr = lv_obj_create(scr);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, UI_W, HDR_H);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(COL_HDR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, LV_PART_MAIN);

    (void)ui_mk_label_center(hdr, 6, UI_FONT(16), COL_HDR_TXT,
                             "STM32H743 信息面板");

    /* ---- Clock -----------------------------------------------------------*/
    g_info.ui.clock = ui_mk_label_center(scr, CLOCK_Y, UI_FONT(32), COL_CLOCK,
                                    "--:--:--");
    g_info.ui.date  = ui_mk_label_center(scr, DATE_Y, UI_FONT(16), COL_DATE,
                                    "---------");

    ui_mk_separator(scr, SEP1_Y);

    /* ---- SD card ---------------------------------------------------------*/
    g_info.ui.sd_head = ui_mk_label(scr, UI_PAD, SD_HEAD_Y, UI_FONT(16),
                               COL_LABEL, "SD卡容量");
    g_info.ui.sd_fs   = ui_mk_label(scr, 0, SD_HEAD_Y, UI_FONT(16), COL_DIM, "--");
    ui_align_right(g_info.ui.sd_fs, SD_HEAD_Y);

    g_info.ui.sd_val = ui_mk_label(scr, UI_PAD, SD_VAL_Y, UI_FONT(16),
                              COL_VALUE, "读取中...");

    g_info.ui.sd_bar = lv_bar_create(scr);
    lv_obj_remove_style_all(g_info.ui.sd_bar);
    lv_obj_set_size(g_info.ui.sd_bar, UI_W - (2 * UI_PAD) - 40, SD_BAR_H);
    lv_obj_set_pos(g_info.ui.sd_bar, UI_PAD, SD_BAR_Y);
    lv_bar_set_range(g_info.ui.sd_bar, 0, 100);
    lv_bar_set_value(g_info.ui.sd_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_info.ui.sd_bar, lv_color_hex(COL_BAR_BG),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_info.ui.sd_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(g_info.ui.sd_bar, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_info.ui.sd_bar, lv_color_hex(COL_ACCENT),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(g_info.ui.sd_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_info.ui.sd_bar, 2, LV_PART_INDICATOR);

    g_info.ui.sd_pct = ui_mk_label(scr, 0, SD_BAR_Y - 3, UI_FONT(12),
                              COL_ACCENT, "--%");
    ui_align_right(g_info.ui.sd_pct, SD_BAR_Y - 3);

    ui_mk_separator(scr, SEP2_Y);

    /* ---- Board info ------------------------------------------------------*/
    g_info.ui.freq = ui_mk_label(scr, UI_PAD, INFO1_Y, UI_FONT(16), COL_LABEL, "");
    lv_label_set_text_fmt(g_info.ui.freq, "主频  %d MHz",
                          (int)(HAL_RCC_GetSysClockFreq() / 1000000U));

    g_info.ui.clksrc = ui_mk_label(scr, 0, INFO1_Y, UI_FONT(16), COL_DIM, "");
    if (g_clock_source == CLOCK_SRC_HSE_XTAL)
    {
        lv_label_set_text(g_info.ui.clksrc, "HSE 25M");
    }
    else
    {
        lv_obj_set_style_text_color(g_info.ui.clksrc, lv_color_hex(COL_ERR),
                                    LV_PART_MAIN);
        lv_label_set_text(g_info.ui.clksrc, "HSI 备用");
    }
    ui_align_right(g_info.ui.clksrc, INFO1_Y);

    g_info.ui.uptime = ui_mk_label(scr, UI_PAD, INFO2_Y, UI_FONT(16),
                              COL_LABEL, "运行  00:00:00");

    /* Font source line doubles as an RTC clock-source readout. */
    g_info.ui.fontinfo = ui_mk_label(scr, UI_PAD, INFO3_Y, UI_FONT(16),
                                COL_LABEL, "");
    uint32_t mask = lcd_driver_font_status();
    lv_label_set_text_fmt(g_info.ui.fontinfo, "字库  %s  时基 %s",
                          lv_font_provider_name(),
                          (drv_rtc_clock_source() == RTC_CLK_LSE) ? "LSE"
                                                                  : "LSI");
    if (mask == 0U)
    {
        lv_obj_set_style_text_color(g_info.ui.fontinfo, lv_color_hex(COL_ERR),
                                    LV_PART_MAIN);
    }

    g_info.ui.cache = ui_mk_label(scr, UI_PAD, INFO4_Y, UI_FONT(12),
                             COL_DIM, "缓存  命中 0 / 读卡 0");

    g_info.uptime_sec   = 0U;
    g_info.sd_countdown = 0U;

    /* First paint with real values, then hand over to the timer. */
    refresh_clock();
    refresh_runtime();

    (void)lv_timer_create(ui_page_info_tick, 1000, NULL);

    return scr;
}

void ui_page_info_request_sd_refresh(void)
{
    g_info.sd_countdown = 0U;
}
