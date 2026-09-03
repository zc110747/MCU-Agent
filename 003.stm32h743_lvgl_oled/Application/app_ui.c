/**
  ******************************************************************************
  * @file    app_ui.c
  * @brief   LVGL screen for the 240x240 ST7789 panel.
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
#include "app_ui.h"
#include "lvgl.h"
#include "lv_font_gbk.h"
#include "lv_font_harmony.h"
#include "lv_font_provider.h"
#include "lv_port_fs.h"
#include "drv_rtc.h"
#include "drv_sdio.h"
#include "drv_oled_text.h"
#include <stdio.h>

/* No label names a concrete font: everything goes through the provider, so the
 * engine switch in lv_font_cfg.h is the only place that has to be edited (or
 * the only CMake variable that has to be flipped). */
#define UI_FONT(px)     lv_font_provider_get((px))

/* ---- Geometry --------------------------------------------------------------*/
#define UI_W                240
#define UI_H                240
#define UI_PAD              8

#define HDR_H               28
#define CLOCK_Y             34
#define DATE_Y              74
#define SEP1_Y              98
#define SD_HEAD_Y           104
#define SD_VAL_Y            124
#define SD_BAR_Y            146
#define SD_BAR_H            8
#define SEP2_Y              160
#define INFO1_Y             166
#define INFO2_Y             186
#define INFO3_Y             206
#define INFO4_Y             226

/* ---- Palette ---------------------------------------------------------------*/
#define COL_BG              0x000000
#define COL_HDR             0x0A3D62
#define COL_HDR_TXT         0xFFD966
#define COL_CLOCK           0x00E5FF
#define COL_DATE            0xFFFFFF
#define COL_LABEL           0x8A8A8A
#define COL_VALUE           0x40E070
#define COL_ACCENT          0xFFA000
#define COL_BAR_BG          0x2A2A2A
#define COL_SEP             0x243447
#define COL_DIM             0x606060
#define COL_ERR             0xFF4040

/* SD capacity is re-read every N refresh ticks (tick = 1 s). */
#define SD_REFRESH_PERIOD   30U

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

static ui_handles_t s_ui;
static uint32_t     s_sd_countdown = 0U;   /* 0 -> query on the next tick */
static uint32_t     s_uptime_sec   = 0U;
static uint8_t      s_built        = 0U;

/*----------------------------------------------------------------------------
 *  Small helpers
 *--------------------------------------------------------------------------*/

/**
  * @brief  Plain label: transparent background, no padding, fixed position.
  */
static lv_obj_t *mk_label(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                          const lv_font_t *font, uint32_t color,
                          const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);

    lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(lbl, 0, LV_PART_MAIN);
    lv_label_set_text(lbl, text);
    lv_obj_set_pos(lbl, x, y);

    return lbl;
}

/**
  * @brief  Same, but horizontally centred on the screen.
  */
static lv_obj_t *mk_label_center(lv_obj_t *parent, lv_coord_t y,
                                 const lv_font_t *font, uint32_t color,
                                 const char *text)
{
    lv_obj_t *lbl = mk_label(parent, 0, y, font, color, text);

    /* Full-width label + centred text keeps the position stable when the
     * string length changes (e.g. 9:05 -> 10:05), which avoids repainting a
     * shifting box every second. */
    lv_obj_set_width(lbl, UI_W);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(lbl, 0, y);

    return lbl;
}

/**
  * @brief  1 px horizontal rule.
  */
static void mk_separator(lv_obj_t *parent, lv_coord_t y)
{
    lv_obj_t *ln = lv_obj_create(parent);

    lv_obj_remove_style_all(ln);
    lv_obj_set_size(ln, UI_W - (2 * UI_PAD), 1);
    lv_obj_set_pos(ln, UI_PAD, y);
    lv_obj_set_style_bg_color(ln, lv_color_hex(COL_SEP), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ln, LV_OPA_COVER, LV_PART_MAIN);
}

/**
  * @brief  Right-align a label against the screen edge without a layout pass.
  */
static void align_right(lv_obj_t *lbl, lv_coord_t y)
{
    lv_obj_set_width(lbl, UI_W - (2 * UI_PAD));
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_pos(lbl, UI_PAD, y);
}

/*----------------------------------------------------------------------------
 *  Data refresh
 *--------------------------------------------------------------------------*/

static void refresh_clock(void)
{
    rtc_datetime_t dt;

    if (drv_rtc_get(&dt) != RT_OK)
    {
        lv_label_set_text(s_ui.clock, "--:--:--");
        lv_label_set_text(s_ui.date, "RTC 未启动");
        return;
    }

    lv_label_set_text_fmt(s_ui.clock, "%02d:%02d:%02d",
                          (int)dt.hour, (int)dt.minute, (int)dt.second);

    lv_label_set_text_fmt(s_ui.date, "%04d-%02d-%02d  %s",
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
        lv_label_set_text(s_ui.sd_val, "读取失败");
        lv_obj_set_style_text_color(s_ui.sd_val, lv_color_hex(COL_ERR),
                                    LV_PART_MAIN);
        lv_label_set_text(s_ui.sd_fs, "--");
        lv_bar_set_value(s_ui.sd_bar, 0, LV_ANIM_OFF);
        lv_label_set_text(s_ui.sd_pct, "--%");
        return;
    }

    lv_obj_set_style_text_color(s_ui.sd_val, lv_color_hex(COL_VALUE),
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

    lv_label_set_text_fmt(s_ui.sd_val, "已用 %s / %s", used_str, total_str);
    lv_label_set_text_fmt(s_ui.sd_fs, "%s %s",
                          drv_sd_card_name(info.card_type),
                          drv_sd_fs_name(info.fs_type));
    lv_bar_set_value(s_ui.sd_bar, (int32_t)pct, LV_ANIM_OFF);
    lv_label_set_text_fmt(s_ui.sd_pct, "%d%%", (int)pct);
}

static void refresh_runtime(void)
{
    uint32_t hits = 0U;
    uint32_t miss = 0U;

    lv_label_set_text_fmt(s_ui.uptime, "运行  %02d:%02d:%02d",
                          (int)(s_uptime_sec / 3600U),
                          (int)((s_uptime_sec / 60U) % 60U),
                          (int)(s_uptime_sec % 60U));

    /* Both engines count "glyph served from RAM" vs "glyph that reached the
     * card"; which one is live decides whose counters we show. */
    if (lv_font_provider_engine() == FONT_ENGINE_HARMONYOS)
    {
        lv_font_harmony_stats(&hits, &miss);
    }
    else
    {
        lv_font_gbk_cache_stats(&hits, &miss);
    }

    lv_label_set_text_fmt(s_ui.cache, "缓存  命中 %lu / 读卡 %lu",
                          (unsigned long)hits, (unsigned long)miss);
}

/**
  * @brief  1 Hz refresh timer.
  */
static void ui_tick_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    s_uptime_sec++;

    refresh_clock();
    refresh_runtime();

    if (s_sd_countdown == 0U)
    {
        refresh_sd();
        s_sd_countdown = SD_REFRESH_PERIOD;
    }
    else
    {
        s_sd_countdown--;
    }
}

/*----------------------------------------------------------------------------
 *  Public API
 *--------------------------------------------------------------------------*/

void app_ui_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_t *hdr;
    uint32_t  mask;

    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr, 0, LV_PART_MAIN);

    /* ---- Header ----------------------------------------------------------*/
    hdr = lv_obj_create(scr);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, UI_W, HDR_H);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(COL_HDR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, LV_PART_MAIN);

    (void)mk_label_center(hdr, 6, UI_FONT(16), COL_HDR_TXT,
                          "STM32H743 信息面板");

    /* ---- Clock -----------------------------------------------------------*/
    s_ui.clock = mk_label_center(scr, CLOCK_Y, UI_FONT(32), COL_CLOCK,
                                 "--:--:--");
    s_ui.date  = mk_label_center(scr, DATE_Y, UI_FONT(16), COL_DATE,
                                 "---------");

    mk_separator(scr, SEP1_Y);

    /* ---- SD card ---------------------------------------------------------*/
    s_ui.sd_head = mk_label(scr, UI_PAD, SD_HEAD_Y, UI_FONT(16),
                            COL_LABEL, "SD卡容量");
    s_ui.sd_fs   = mk_label(scr, 0, SD_HEAD_Y, UI_FONT(16), COL_DIM, "--");
    align_right(s_ui.sd_fs, SD_HEAD_Y);

    s_ui.sd_val = mk_label(scr, UI_PAD, SD_VAL_Y, UI_FONT(16),
                           COL_VALUE, "读取中...");

    s_ui.sd_bar = lv_bar_create(scr);
    lv_obj_remove_style_all(s_ui.sd_bar);
    lv_obj_set_size(s_ui.sd_bar, UI_W - (2 * UI_PAD) - 40, SD_BAR_H);
    lv_obj_set_pos(s_ui.sd_bar, UI_PAD, SD_BAR_Y);
    lv_bar_set_range(s_ui.sd_bar, 0, 100);
    lv_bar_set_value(s_ui.sd_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_ui.sd_bar, lv_color_hex(COL_BAR_BG),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.sd_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_ui.sd_bar, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.sd_bar, lv_color_hex(COL_ACCENT),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_ui.sd_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_ui.sd_bar, 2, LV_PART_INDICATOR);

    s_ui.sd_pct = mk_label(scr, 0, SD_BAR_Y - 3, UI_FONT(12),
                           COL_ACCENT, "--%");
    align_right(s_ui.sd_pct, SD_BAR_Y - 3);

    mk_separator(scr, SEP2_Y);

    /* ---- Board info ------------------------------------------------------*/
    s_ui.freq = mk_label(scr, UI_PAD, INFO1_Y, UI_FONT(16), COL_LABEL, "");
    lv_label_set_text_fmt(s_ui.freq, "主频  %d MHz",
                          (int)(HAL_RCC_GetSysClockFreq() / 1000000U));

    s_ui.clksrc = mk_label(scr, 0, INFO1_Y, UI_FONT(16), COL_DIM, "");
    if (g_clock_source == CLOCK_SRC_HSE_XTAL)
    {
        lv_label_set_text(s_ui.clksrc, "HSE 25M");
    }
    else
    {
        lv_obj_set_style_text_color(s_ui.clksrc, lv_color_hex(COL_ERR),
                                    LV_PART_MAIN);
        lv_label_set_text(s_ui.clksrc, "HSI 备用");
    }
    align_right(s_ui.clksrc, INFO1_Y);

    s_ui.uptime = mk_label(scr, UI_PAD, INFO2_Y, UI_FONT(16),
                           COL_LABEL, "运行  00:00:00");

    /* Font source line doubles as an RTC clock-source readout. */
    s_ui.fontinfo = mk_label(scr, UI_PAD, INFO3_Y, UI_FONT(16),
                             COL_LABEL, "");
    mask = lcd_driver_font_status();
    lv_label_set_text_fmt(s_ui.fontinfo, "字库  %s  时基 %s",
                          lv_font_provider_name(),
                          (drv_rtc_clock_source() == RTC_CLK_LSE) ? "LSE"
                                                                  : "LSI");
    if (mask == 0U)
    {
        lv_obj_set_style_text_color(s_ui.fontinfo, lv_color_hex(COL_ERR),
                                    LV_PART_MAIN);
    }

    s_ui.cache = mk_label(scr, UI_PAD, INFO4_Y, UI_FONT(12),
                          COL_DIM, "缓存  命中 0 / 读卡 0");

    s_built        = 1U;
    s_uptime_sec   = 0U;
    s_sd_countdown = 0U;

    /* First paint with real values, then hand over to the timer. */
    refresh_clock();
    refresh_runtime();

    (void)lv_timer_create(ui_tick_cb, 1000, NULL);
}

void app_ui_show_fault(const char *line1, const char *line2, const char *line3)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_t *hdr;

    lv_obj_clean(scr);
    s_built = 0U;

    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    hdr = lv_obj_create(scr);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, UI_W, HDR_H);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x7A1010), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, LV_PART_MAIN);

    /* ASCII only from here down - the Chinese glyphs live on the card that
     * just failed to come up. */
    (void)mk_label_center(hdr, 6, UI_FONT(16), 0xFFFFFF, "SD / FONT ERROR");

    (void)mk_label(scr, UI_PAD, 50,  UI_FONT(16), 0xFFD966,
                   (line1 != NULL) ? line1 : "");
    (void)mk_label(scr, UI_PAD, 74,  UI_FONT(16), 0xFFFFFF,
                   (line2 != NULL) ? line2 : "");
    (void)mk_label(scr, UI_PAD, 98,  UI_FONT(16), 0xFFFFFF,
                   (line3 != NULL) ? line3 : "");

    (void)mk_label(scr, UI_PAD, 140, UI_FONT(12), 0x80D0FF,
                   "Expected on the card:");
    (void)mk_label(scr, UI_PAD, 158, UI_FONT(12), 0xB0B0B0,
                   "1:/SYSTEM/FONT/UNIGBK.BIN");
    (void)mk_label(scr, UI_PAD, 174, UI_FONT(12), 0xB0B0B0,
                   "1:/SYSTEM/FONT/GBK12.FON");
    (void)mk_label(scr, UI_PAD, 190, UI_FONT(12), 0xB0B0B0,
                   "1:/SYSTEM/FONT/GBK16.FON");
    (void)mk_label(scr, UI_PAD, 206, UI_FONT(12), 0xB0B0B0,
                   "1:/SYSTEM/FONT/GBK24.FON");
    (void)mk_label(scr, UI_PAD, 222, UI_FONT(12), 0xB0B0B0,
                   "1:/SYSTEM/FONT/GBK32.FON");
}

void app_ui_request_sd_refresh(void)
{
    if (s_built != 0U)
    {
        s_sd_countdown = 0U;
    }
}
