/**
  ******************************************************************************
  * @file    app_ui.c
  * @brief   LVGL screen for the STM32F429 800x400 panel (minimal dark theme).
  ******************************************************************************
  */
#include "app_ui.h"
#include "lvgl.h"
#include "lv_font_gbk.h"
#include "bsp_lcd_text.h"
#include "usb_host_app.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>

/* ---- Geometry -------------------------------------------------------------- */
#define UI_W      800
#define UI_H      400
#define UI_PAD    12

#define HDR_H     32
#define TITLE_Y   6
#define USB_Y     56
#define FONT_Y    100
#define FREQ_Y    144
#define UPTIME_Y  188
#define CACHE_Y   232
#define SEP1_Y    84
#define SEP2_Y    168

/* ---- Palette (minimal: dark bg, light text, no accents) -------------------- */
#define COL_BG    0x000000
#define COL_HDR   0x12161C
#define COL_TXT   0xD2D6DC
#define COL_DIM   0x808890

/* 1 Hz refresh tick drives the dynamic lines. */
#define UI_REFRESH_MS  1000U

typedef struct
{
    lv_obj_t *usb;
    lv_obj_t *font;
    lv_obj_t *freq;
    lv_obj_t *uptime;
    lv_obj_t *cache;
} ui_handles_t;

static ui_handles_t s_ui;
static uint32_t     s_uptime_sec = 0U;
static uint8_t      s_built      = 0U;
static uint8_t      s_refresh_req = 0U;

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

    lv_obj_set_width(lbl, UI_W);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(lbl, 0, y);

    return lbl;
}

static void mk_separator(lv_obj_t *parent, lv_coord_t y)
{
    lv_obj_t *ln = lv_obj_create(parent);

    lv_obj_remove_style_all(ln);
    lv_obj_set_size(ln, UI_W - (2 * UI_PAD), 1);
    lv_obj_set_pos(ln, UI_PAD, y);
    lv_obj_set_style_bg_color(ln, lv_color_hex(COL_DIM), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ln, LV_OPA_COVER, LV_PART_MAIN);
}

static void align_right(lv_obj_t *lbl, lv_coord_t y)
{
    lv_obj_set_width(lbl, UI_W - (2 * UI_PAD));
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_pos(lbl, UI_PAD, y);
}

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

/*----------------------------------------------------------------------------*/
/* Data refresh                                                               */
/*----------------------------------------------------------------------------*/
static void refresh_usb(void)
{
    lv_label_set_text_fmt(s_ui.usb, "USB 状态  %s", usb_state_str(g_usb_state));
}

static void refresh_font(void)
{
    uint32_t mask = lcd_driver_font_status();
    const char *status;

    if (mask == 0U)
    {
        status = "字库  未加载 (请在 U 盘放入 SYSTEM/FONT/GBKxx.FON)";
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
    lv_label_set_text(s_ui.font, status);
}

static void refresh_runtime(void)
{
    uint32_t hits = 0U;
    uint32_t miss = 0U;

    lv_label_set_text_fmt(s_ui.uptime, "运行  %02d:%02d:%02d",
                          (int)(s_uptime_sec / 3600U),
                          (int)((s_uptime_sec / 60U) % 60U),
                          (int)(s_uptime_sec % 60U));

    lv_font_gbk_cache_stats(&hits, &miss);
    lv_label_set_text_fmt(s_ui.cache, "缓存  命中 %d / 读卡 %d",
                          (int)hits, (int)miss);
}

static void ui_tick_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    s_uptime_sec++;
    refresh_usb();
    refresh_runtime();

    if (s_refresh_req != 0U)
    {
        refresh_font();
        s_refresh_req = 0U;
    }
}

/*----------------------------------------------------------------------------*/
/* Public API                                                                 */
/*----------------------------------------------------------------------------*/
void app_ui_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_t *hdr;

    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr, 0, LV_PART_MAIN);

    hdr = lv_obj_create(scr);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, UI_W, HDR_H);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(COL_HDR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, LV_PART_MAIN);

    (void)mk_label_center(hdr, TITLE_Y, &lv_font_gbk_24, COL_TXT,
                          "STM32F429 信息面板");

    s_ui.usb   = mk_label(scr, UI_PAD, USB_Y,  &lv_font_gbk_24, COL_TXT, "USB 状态  --");
    s_ui.font  = mk_label(scr, UI_PAD, FONT_Y, &lv_font_gbk_16, COL_TXT, "字库  --");

    mk_separator(scr, SEP1_Y);

    s_ui.freq  = mk_label(scr, UI_PAD, FREQ_Y, &lv_font_gbk_16, COL_TXT, "");
    lv_label_set_text_fmt(s_ui.freq, "主频  %d MHz",
                          (int)(HAL_RCC_GetSysClockFreq() / 1000000U));

    mk_separator(scr, SEP2_Y);

    s_ui.uptime = mk_label(scr, UI_PAD, UPTIME_Y, &lv_font_gbk_16, COL_TXT,
                           "运行  00:00:00");
    s_ui.cache  = mk_label(scr, UI_PAD, CACHE_Y, &lv_font_gbk_16, COL_DIM,
                           "缓存  命中 0 / 读卡 0");

    s_built = 1U;
    s_uptime_sec = 0U;
    s_refresh_req = 1U;   /* re-read font status on first tick */

    refresh_usb();
    refresh_font();
    refresh_runtime();

    (void)lv_timer_create(ui_tick_cb, UI_REFRESH_MS, NULL);
}

void app_ui_show_fault(const char *line1, const char *line2, const char *line3)
{
    lv_obj_t *scr = lv_scr_act();

    lv_obj_clean(scr);
    s_built = 0U;

    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    (void)mk_label_center(scr, TITLE_Y, &lv_font_gbk_24, COL_TXT, "USB / 字库错误");

    (void)mk_label(scr, UI_PAD, 70,  &lv_font_gbk_16, COL_TXT,
                   (line1 != NULL) ? line1 : "");
    (void)mk_label(scr, UI_PAD, 110, &lv_font_gbk_16, COL_TXT,
                   (line2 != NULL) ? line2 : "");
    (void)mk_label(scr, UI_PAD, 150, &lv_font_gbk_16, COL_TXT,
                   (line3 != NULL) ? line3 : "");

    (void)mk_label(scr, UI_PAD, 210, &lv_font_gbk_16, COL_DIM, "U 盘预期文件:");
    (void)mk_label(scr, UI_PAD, 240, &lv_font_gbk_16, COL_DIM, "0:/SYSTEM/FONT/UNIGBK.BIN");
    (void)mk_label(scr, UI_PAD, 264, &lv_font_gbk_16, COL_DIM, "0:/SYSTEM/FONT/GBK12.FON .. GBK32.FON");
}

void app_ui_request_usb_refresh(void)
{
    if (s_built != 0U)
    {
        s_refresh_req = 1U;
    }
}
