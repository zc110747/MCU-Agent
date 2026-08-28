/**
  ******************************************************************************
  * @file    app_ui.c
  * @brief   Multi-page LVGL screen for the STM32F429 panel (minimal dark theme).
  *
  *  Geometry
  *  --------
  *  Nothing here is hardcoded to 800x480.  The layout is derived from the
  *  resolution the LVGL display was actually registered with, which is the
  *  active GRAM window reported by the LCD driver (480x800 on the NT35510
  *  module this project is built for).  Deriving it means the panel fills the
  *  glass whatever orientation the controller ends up in, and it is the one
  *  place that has to change if LCD_WIDTH / LCD_HEIGHT are ever retuned.
  *
  *  Pages
  *  -----
  *    0  状态       系统初始化 / 运行信息 / 故障·消息
  *    1  硬件信息    AP3216C 与 MPU9250 实时读数
  *
  *  The bottom navigation bar is shared by both pages.  Its left and right
  *  buttons are drawn as LVGL line chevrons rather than text glyphs, so they
  *  render identically with or without the SD-card font files.
  ******************************************************************************
  */
#include "app_ui.h"
#include "lvgl.h"
#include "lv_font_gbk.h"
#include "bsp_lcd.h"
#include "bsp_lcd_text.h"
#include "bsp_touch.h"
#include "usb_host_app.h"
#include "sd_card.h"
#include "sensor_task.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* ---- Geometry -------------------------------------------------------------- */
#define UI_PAD      12
#define HDR_H       34
#define NAV_H       56
#define BAND_GAP    10
#define TOP_GAP     8

/* ---- Nav bar --------------------------------------------------------------- */
#define NAV_BTN_W   72
#define NAV_BTN_H   44

/* ---- Palette (minimal: dark bg, light text, no accents) -------------------- */
#define COL_BG      0x000000
#define COL_HDR     0x12161C
#define COL_TXT     0xD2D6DC
#define COL_DIM     0x808890
#define COL_BTN     0x1C222B

/* 2 Hz refresh: snappy enough for the sensor page, cheap enough for the pump. */
#define UI_REFRESH_MS  500U

typedef enum
{
    PAGE_STATUS = 0,
    PAGE_HWINFO = 1
} ui_page_t;

/* Chevron point sets for the two navigation buttons.  lv_line keeps its own
 * coordinate space, so these are relative to the line object's top-left. */
static const lv_point_t s_chevron_left[3]  = { { 14, 2 }, { 5, 10 }, { 14, 18 } };
static const lv_point_t s_chevron_right[3] = { { 6, 2 }, { 15, 10 }, { 6, 18 } };

typedef struct
{
    /* page 0 - status */
    lv_obj_t *p0_sd;
    lv_obj_t *p0_usb;
    lv_obj_t *p0_font;
    lv_obj_t *p0_freq;
    lv_obj_t *p0_uptime;
    lv_obj_t *p0_cache;
    lv_obj_t *p0_f1;
    lv_obj_t *p0_f2;
    lv_obj_t *p0_f3;
    /* page 1 - hardware information */
    lv_obj_t *p1_ir;
    lv_obj_t *p1_als;
    lv_obj_t *p1_ps;
    lv_obj_t *p1_acc;
    lv_obj_t *p1_gyr;
    lv_obj_t *p1_mag;
    lv_obj_t *p1_stat;
    /* shared */
    lv_obj_t *page_lbl;
} ui_handles_t;

static ui_handles_t s_ui;
static lv_timer_t  *s_timer      = NULL;
static uint32_t     s_uptime_sec = 0U;
static uint32_t     s_last_sec_at = 0U;
static uint8_t      s_built      = 0U;
static uint8_t      s_refresh_req = 0U;
static int          s_page       = (int)PAGE_STATUS;

/* Derived layout, recomputed on every rebuild. */
static lv_coord_t   s_w       = 0;
static lv_coord_t   s_h       = 0;
static lv_coord_t   s_band_y[3];
static lv_coord_t   s_band_h  = 0;
static lv_coord_t   s_nav_y   = 0;

/*----------------------------------------------------------------------------*/
/* Helpers                                                                    */
/*----------------------------------------------------------------------------*/
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

static lv_obj_t *mk_label_center(lv_obj_t *parent, lv_coord_t y,
                                const lv_font_t *font, uint32_t color,
                                const char *text)
{
    lv_obj_t *lbl = mk_label(parent, 0, y, font, color, text);

    lv_obj_set_width(lbl, s_w);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(lbl, 0, y);

    return lbl;
}

static lv_obj_t *make_band(lv_obj_t *parent, lv_coord_t y, lv_coord_t h,
                           const char *title)
{
    lv_obj_t *card = lv_obj_create(parent);

    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, s_w - (2 * UI_PAD), h);
    lv_obj_set_pos(card, UI_PAD, y);
    lv_obj_set_style_bg_color(card, lv_color_hex(COL_HDR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(COL_DIM), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    (void)mk_label(card, 8, 6, &lv_font_gbk_16, COL_TXT, title);

    return card;
}

static void ui_set_screen_bg(lv_obj_t *scr)
{
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr, 0, LV_PART_MAIN);
}

/**
  * @brief  Format a float with two decimals without pulling in the floating
  *         point printf support of newlib-nano (which is not linked).
  */
static void fmt_fixed2(char *buf, size_t n, float v)
{
    int  neg = (v < 0.0f) ? 1 : 0;
    float a  = neg ? -v : v;
    long ip  = (long)a;
    long fp  = (long)((a - (float)ip) * 100.0f + 0.5f);

    if (fp >= 100L) { fp -= 100L; ip += 1L; }
    (void)snprintf(buf, n, "%s%ld.%02ld", neg ? "-" : "", ip, fp);
}

static void fmt_vec3(char *buf, size_t n, const char *name,
                     float a, float b, float c, const char *unit)
{
    char fa[16], fb[16], fc[16];

    fmt_fixed2(fa, sizeof(fa), a);
    fmt_fixed2(fb, sizeof(fb), b);
    fmt_fixed2(fc, sizeof(fc), c);
    (void)snprintf(buf, n, "%s  X %s  Y %s  Z %s %s", name, fa, fb, fc, unit);
}

/**
  * @brief  Delete the refresh timer, then wipe the active screen.
  *         Order matters (see the file header).
  */
static void ui_teardown(void)
{
    if (s_timer != NULL)
    {
        lv_timer_del(s_timer);
        s_timer = NULL;
    }
    lv_obj_clean(lv_scr_act());
    (void)memset(&s_ui, 0, sizeof(s_ui));
    s_built = 0U;
}

/**
  * @brief  Recompute the layout from the resolution the display is registered
  *         with.  Done on every rebuild so a resolution change needs no edits.
  */
static void ui_layout(void)
{
    lv_coord_t content_y;
    lv_coord_t content_h;
    int i;

    s_w = (lv_coord_t)lv_disp_get_hor_res(NULL);
    s_h = (lv_coord_t)lv_disp_get_ver_res(NULL);

    if (s_w <= 0) { s_w = 1; }
    if (s_h <= 0) { s_h = 1; }

    content_y = HDR_H + TOP_GAP;
    content_h = s_h - content_y - NAV_H - TOP_GAP;
    if (content_h < (3 * BAND_GAP))
    {
        content_h = 3 * BAND_GAP;
    }

    s_band_h = (lv_coord_t)((content_h - (2 * BAND_GAP)) / 3);
    for (i = 0; i < 3; i++)
    {
        s_band_y[i] = (lv_coord_t)(content_y + (i * (s_band_h + BAND_GAP)));
    }

    s_nav_y = (lv_coord_t)(s_h - NAV_H);
}

/*----------------------------------------------------------------------------*/
/* Navigation bar                                                             */
/*----------------------------------------------------------------------------*/
static lv_obj_t *mk_nav_button(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                               const lv_point_t *chevron)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_t *line;

    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, NAV_BTN_W, NAV_BTN_H);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COL_BTN), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(COL_DIM), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    /* The chevron: a 20x20 line object centred in the button.  lv_line clears
     * LV_OBJ_FLAG_CLICKABLE in its constructor, so taps fall through to the
     * button behind it. */
    line = lv_line_create(btn);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, 20, 20);
    lv_obj_set_pos(line, (NAV_BTN_W - 20) / 2, (NAV_BTN_H - 20) / 2);
    lv_obj_set_style_line_width(line, 3, LV_PART_MAIN);
    lv_obj_set_style_line_color(line, lv_color_hex(COL_TXT), LV_PART_MAIN);
    lv_obj_set_style_line_rounded(line, 1, LV_PART_MAIN);
    lv_line_set_points(line, chevron, 3);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);

    return btn;
}

/*----------------------------------------------------------------------------*/
/* Data refresh                                                               */
/*----------------------------------------------------------------------------*/
static const char *usb_state_str(usb_state_t s)
{
    switch (s)
    {
        case USB_DISCONNECTED: return "未连接";
        case USB_CONNECTED:     return "已连接";
        case USB_ENUMERATED:    return "已枚举";
        case USB_MSC_READY:     return "MSC 就绪";
        case USB_MOUNTED:       return "已挂载";
        case USB_ERROR:         return "错误";
        default:                return "未知";
    }
}

static void refresh_usb(void)
{
    if (s_ui.p0_usb != NULL)
    {
        lv_label_set_text_fmt(s_ui.p0_usb, "USB 状态  %s", usb_state_str(g_usb_state));
    }
}

static void refresh_font(void)
{
    uint32_t mask = lcd_driver_font_status();
    const char *src = lcd_driver_font_source();
    const char *status;

    if (s_ui.p0_font == NULL)
    {
        return;
    }

    if (mask == 0U)
    {
        lv_label_set_text(s_ui.p0_font, "字库  未加载");
        return;
    }

    if (src[0] == '1')
    {
        status = "字库  SD 卡 GBK12/16/24/32 已就绪";
    }
    else if (src[0] == '0')
    {
        status = "字库  U 盘 GBK12/16/24/32 已就绪";
    }
    else if ((mask & (FONT_MASK_GBK12 | FONT_MASK_GBK16 |
                      FONT_MASK_GBK24 | FONT_MASK_GBK32)) ==
             (FONT_MASK_GBK12 | FONT_MASK_GBK16 |
              FONT_MASK_GBK24 | FONT_MASK_GBK32))
    {
        status = "字库  GBK12/16/24/32 已就绪";
    }
    else
    {
        status = "字库  部分就绪";
    }
    lv_label_set_text(s_ui.p0_font, status);
}

static void refresh_sd(void)
{
    if (s_ui.p0_sd == NULL)
    {
        return;
    }

    if (sd_card_is_ready() != 0)
    {
        lv_label_set_text_fmt(s_ui.p0_sd, "SD 卡  %lu MB (SDIO 4bit)",
                              (unsigned long)sd_card_capacity_mb());
    }
    else
    {
        lv_label_set_text(s_ui.p0_sd, "SD 卡  未检测到");
    }
}

static void refresh_runtime(void)
{
    uint32_t hits = 0U;
    uint32_t miss = 0U;

    if (s_ui.p0_uptime != NULL)
    {
        lv_label_set_text_fmt(s_ui.p0_uptime, "运行  %02d:%02d:%02d",
                              (int)(s_uptime_sec / 3600U),
                              (int)((s_uptime_sec / 60U) % 60U),
                              (int)(s_uptime_sec % 60U));
    }

    if (s_ui.p0_cache != NULL)
    {
        lv_font_gbk_cache_stats(&hits, &miss);
        lv_label_set_text_fmt(s_ui.p0_cache, "缓存  命中 %d / 读卡 %d",
                              (int)hits, (int)miss);
    }
}

static void refresh_hwinfo(void)
{
    sensor_data_t d;
    char buf[80];

    sensor_get(&d);

    if (s_ui.p1_ir != NULL)
    {
        if (d.ap3216_ok != 0U)
        {
            lv_label_set_text_fmt(s_ui.p1_ir, "红外 IR  %u", (unsigned int)d.ir);
            lv_label_set_text_fmt(s_ui.p1_als, "环境光  %u lux", (unsigned int)d.als);
            lv_label_set_text_fmt(s_ui.p1_ps, "接近  %u", (unsigned int)d.ps);
        }
        else
        {
            lv_label_set_text(s_ui.p1_ir, "红外 IR  --");
            lv_label_set_text(s_ui.p1_als, "环境光  --");
            lv_label_set_text(s_ui.p1_ps, "接近  --");
        }
    }

    if (s_ui.p1_acc != NULL)
    {
        if (d.mpu_ok != 0U)
        {
            fmt_vec3(buf, sizeof(buf), "加速度", d.ax, d.ay, d.az, "g");
            lv_label_set_text(s_ui.p1_acc, buf);

            fmt_vec3(buf, sizeof(buf), "角速度", d.gx, d.gy, d.gz, "dps");
            lv_label_set_text(s_ui.p1_gyr, buf);

            if (d.mag_ok != 0U)
            {
                fmt_vec3(buf, sizeof(buf), "磁场", d.mx, d.my, d.mz, "uT");
                lv_label_set_text(s_ui.p1_mag, buf);
            }
            else
            {
                lv_label_set_text(s_ui.p1_mag, "磁场  AK8963 未就绪");
            }
        }
        else
        {
            lv_label_set_text(s_ui.p1_acc, "加速度  --");
            lv_label_set_text(s_ui.p1_gyr, "角速度  --");
            lv_label_set_text(s_ui.p1_mag, "磁场  --");
        }
    }

    if (s_ui.p1_stat != NULL)
    {
        lv_label_set_text_fmt(s_ui.p1_stat, "采样  成功 %lu / 失败 %lu  触摸事件 %lu",
                              (unsigned long)d.samples, (unsigned long)d.errors,
                              (unsigned long)bsp_touch_press_count());
    }
}

static void ui_tick_cb(lv_timer_t *timer)
{
    uint32_t now = HAL_GetTick();

    LV_UNUSED(timer);

    if ((now - s_last_sec_at) >= 1000U)
    {
        s_last_sec_at = now;
        s_uptime_sec++;
    }

    if (s_page == (int)PAGE_STATUS)
    {
        refresh_usb();
        refresh_runtime();

        if (s_refresh_req != 0U)
        {
            refresh_font();
            s_refresh_req = 0U;
        }
    }
    else
    {
        refresh_hwinfo();
    }
}

/*----------------------------------------------------------------------------*/
/* Page switch                                                                */
/*----------------------------------------------------------------------------*/
static void nav_btn_cb(lv_event_t *e)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(e);

    app_ui_switch_page(delta);
}

static void build_nav(void)
{
    lv_obj_t *scr  = lv_scr_act();
    lv_obj_t *bar;
    lv_obj_t *btn_l;
    lv_obj_t *btn_r;
    lv_coord_t bar_w = (lv_coord_t)(s_w - (2 * UI_PAD));
    lv_coord_t bar_h = (lv_coord_t)(NAV_H - 8);
    lv_coord_t btn_y = (lv_coord_t)((bar_h - NAV_BTN_H) / 2);

    bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, bar_w, bar_h);
    lv_obj_set_pos(bar, UI_PAD, s_nav_y);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COL_HDR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(bar, lv_color_hex(COL_DIM), LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bar, 0, LV_PART_MAIN);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    btn_l = mk_nav_button(bar, 6, btn_y, s_chevron_left);
    lv_obj_add_event_cb(btn_l, nav_btn_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)-1);

    btn_r = mk_nav_button(bar, (lv_coord_t)(bar_w - NAV_BTN_W - 6), btn_y,
                          s_chevron_right);
    lv_obj_add_event_cb(btn_r, nav_btn_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)1);

    s_ui.page_lbl = mk_label(bar, 0, (lv_coord_t)((bar_h - 20) / 2),
                             &lv_font_gbk_16, COL_DIM, "");
    lv_obj_set_width(s_ui.page_lbl,
                     (lv_coord_t)(bar_w - (2 * NAV_BTN_W) - 24));
    lv_obj_set_pos(s_ui.page_lbl, (lv_coord_t)(NAV_BTN_W + 12),
                   (lv_coord_t)((bar_h - 20) / 2));
    lv_obj_set_style_text_align(s_ui.page_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text_fmt(s_ui.page_lbl, "%d / %d",
                          (int)(s_page + 1), (int)UI_PAGE_COUNT);
}

/*----------------------------------------------------------------------------*/
/* Frame builders                                                             */
/*----------------------------------------------------------------------------*/
static void build_header(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_t *hdr;

    hdr = lv_obj_create(scr);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, s_w, HDR_H);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(COL_HDR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, LV_PART_MAIN);
    (void)mk_label_center(hdr, 5, &lv_font_gbk_24, COL_TXT, "STM32F429 信息面板");
}

static void build_page_status(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_t *b1;
    lv_obj_t *b2;
    lv_obj_t *b3;
    LCD_INFO *info;

    /* Band 1 - 系统初始化 (hardware init summary). */
    b1 = make_band(scr, s_band_y[0], s_band_h, "系统初始化");
    info = get_lcd_info();
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "LCD 控制器  ID = 0x%04X",
                 (unsigned int)((info != NULL) ? info->lcd_id : 0U));
        (void)mk_label(b1, 8, 32, &lv_font_gbk_16, COL_TXT, buf);
        s_ui.p0_sd = mk_label(b1, 8, 56, &lv_font_gbk_16, COL_TXT, "SD 卡  --");
        (void)mk_label(b1, 8, 80, &lv_font_gbk_16, COL_TXT, "USB 主机  已初始化");
        (void)mk_label(b1, 8, 104, &lv_font_gbk_16, COL_DIM, "LVGL 渲染  已就绪");
    }

    /* Band 2 - 运行信息. */
    b2 = make_band(scr, s_band_y[1], s_band_h, "运行信息");
    s_ui.p0_usb   = mk_label(b2, 8, 32,  &lv_font_gbk_16, COL_TXT, "USB 状态  --");
    s_ui.p0_font  = mk_label(b2, 8, 56,  &lv_font_gbk_16, COL_TXT, "字库  --");
    s_ui.p0_freq  = mk_label(b2, 8, 80,  &lv_font_gbk_16, COL_TXT, "");
    lv_label_set_text_fmt(s_ui.p0_freq, "主频  %d MHz",
                          (int)(HAL_RCC_GetSysClockFreq() / 1000000U));
    s_ui.p0_uptime = mk_label(b2, 8, 104, &lv_font_gbk_16, COL_TXT, "运行  00:00:00");
    s_ui.p0_cache  = mk_label(b2, 8, 128, &lv_font_gbk_16, COL_DIM, "缓存  命中 0 / 读卡 0");

    /* Band 3 - 故障/消息. */
    b3 = make_band(scr, s_band_y[2], s_band_h, "故障 / 消息");
    s_ui.p0_f1 = mk_label(b3, 8, 32, &lv_font_gbk_16, COL_TXT, "系统正常");
    s_ui.p0_f2 = mk_label(b3, 8, 56, &lv_font_gbk_16, COL_TXT, "");
    s_ui.p0_f3 = mk_label(b3, 8, 80, &lv_font_gbk_16, COL_TXT, "");
}

static void build_page_hwinfo(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_t *b1;
    lv_obj_t *b2;
    lv_obj_t *b3;

    /* Band 1 - AP3216C. */
    b1 = make_band(scr, s_band_y[0], s_band_h, "环境传感器  AP3216C");
    s_ui.p1_ir  = mk_label(b1, 8, 32, &lv_font_gbk_16, COL_TXT, "红外 IR  --");
    s_ui.p1_als = mk_label(b1, 8, 56, &lv_font_gbk_16, COL_TXT, "环境光  --");
    s_ui.p1_ps  = mk_label(b1, 8, 80, &lv_font_gbk_16, COL_TXT, "接近  --");

    /* Band 2 - MPU9250 accel + gyro. */
    b2 = make_band(scr, s_band_y[1], s_band_h, "运动传感器  MPU9250");
    s_ui.p1_acc = mk_label(b2, 8, 32, &lv_font_gbk_16, COL_TXT, "加速度  --");
    s_ui.p1_gyr = mk_label(b2, 8, 56, &lv_font_gbk_16, COL_TXT, "角速度  --");
    s_ui.p1_mag = mk_label(b2, 8, 80, &lv_font_gbk_16, COL_TXT, "磁场  --");

    /* Band 3 - sampling health. */
    b3 = make_band(scr, s_band_y[2], s_band_h, "采样状态");
    s_ui.p1_stat = mk_label(b3, 8, 32, &lv_font_gbk_16, COL_DIM, "采样  --");
    (void)mk_label(b3, 8, 56, &lv_font_gbk_16, COL_DIM, "I2C2  PH4(SCL) / PH5(SDA) 400kHz");
}

static void ui_build(void)
{
    lv_obj_t *scr = lv_scr_act();

    ui_layout();
    ui_set_screen_bg(scr);

    build_header();

    if (s_page == (int)PAGE_HWINFO)
    {
        build_page_hwinfo();
    }
    else
    {
        build_page_status();
    }

    build_nav();

    s_built = 1U;
}

/**
  * @brief  Re-create every widget without touching the refresh timer.
  *         Used by the page switch; the timer keeps running and only refreshes
  *         the labels that belong to the page that is on screen.
  */
static void ui_rebuild(void)
{
    lv_obj_clean(lv_scr_act());
    (void)memset(&s_ui, 0, sizeof(s_ui));
    s_built = 0U;
    ui_build();
}

/*----------------------------------------------------------------------------*/
/* Public API                                                                 */
/*----------------------------------------------------------------------------*/
void app_ui_create(void)
{
    if (s_built == 0U)
    {
        /* Clear any boot/failure screen that may still be on the display. */
        ui_layout();
        lv_obj_clean(lv_scr_act());
        (void)memset(&s_ui, 0, sizeof(s_ui));
        ui_build();
    }

    s_page       = (int)PAGE_STATUS;
    s_uptime_sec = 0U;
    s_last_sec_at = HAL_GetTick();
    s_refresh_req = 1U;   /* re-read font status on first tick */

    refresh_sd();
    refresh_font();
    refresh_usb();
    refresh_runtime();

    if (s_timer == NULL)
    {
        s_timer = lv_timer_create(ui_tick_cb, UI_REFRESH_MS, NULL);
    }
}

void app_ui_show_centered(const char *text)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_t *lbl;

    ui_teardown();
    ui_layout();
    ui_set_screen_bg(scr);

    /* ASCII-only: the glyphs come from the compiled-in ASCII tables, so this
     * screen renders correctly with no font file and no filesystem. */
    lbl = mk_label_center(scr, 0, &lv_font_gbk_24, COL_TXT,
                          (text != NULL) ? text : "");
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
}

void app_ui_show_fault(const char *line1, const char *line2, const char *line3)
{
    if (s_built == 0U)
    {
        ui_layout();
        lv_obj_clean(lv_scr_act());
        (void)memset(&s_ui, 0, sizeof(s_ui));
        ui_build();
    }

    if (s_ui.p0_f1 != NULL)
    {
        lv_label_set_text(s_ui.p0_f1,
                          (line1 != NULL && line1[0] != '\0') ? line1 : "");
    }
    if (s_ui.p0_f2 != NULL)
    {
        lv_label_set_text(s_ui.p0_f2,
                          (line2 != NULL && line2[0] != '\0') ? line2 : "");
    }
    if (s_ui.p0_f3 != NULL)
    {
        lv_label_set_text(s_ui.p0_f3,
                          (line3 != NULL && line3[0] != '\0') ? line3 : "");
    }
}

void app_ui_request_usb_refresh(void)
{
    if (s_built != 0U)
    {
        s_refresh_req = 1U;
    }
}

void app_ui_switch_page(int delta)
{
    int next = s_page + delta;

    /* Cyclic: wrap into [0, UI_PAGE_COUNT) in both directions. */
    next %= (int)UI_PAGE_COUNT;
    if (next < 0)
    {
        next += (int)UI_PAGE_COUNT;
    }

    if (next == s_page)
    {
        return;
    }

    s_page = next;
    ui_rebuild();

    /* Republish the values that the new page shows. */
    if (s_page == (int)PAGE_STATUS)
    {
        refresh_usb();
        refresh_sd();
        refresh_font();
        refresh_runtime();
    }
    else
    {
        refresh_hwinfo();
    }

    printf("[UI  ] page -> %d / %d\r\n",
           (int)(s_page + 1), (int)UI_PAGE_COUNT);
}

int app_ui_page(void)
{
    return s_page;
}
