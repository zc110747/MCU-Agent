/**
  ******************************************************************************
  * @file    app_ui.c
  * @brief   LVGL screen for the STM32F429 800x400 panel (minimal dark theme).
  *
  *  The screen is laid out as THREE independent, bordered bands so the
  *  initialization area, the runtime info panel (app_ui_create) and the
  *  fault/message area (app_ui_show_fault) never overlap:
  *    Band 1  系统初始化  - LCD controller ID / USB host / LVGL ready
  *    Band 2  运行信息    - USB state, font, freq, uptime, cache (app_ui_create)
  *    Band 3  故障/消息   - wait/error text (app_ui_show_fault), else 系统正常
  *  The frame is built once; state changes only update label text, never
  *  lv_obj_clean(), so the three regions stay separated.
  ******************************************************************************
  */
#include "app_ui.h"
#include "lvgl.h"
#include "lv_font_gbk.h"
#include "bsp_lcd.h"
#include "bsp_lcd_text.h"
#include "usb_host_app.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>

/* ---- Geometry -------------------------------------------------------------- */
#define UI_W      800
#define UI_H      400
#define UI_PAD    12

/* Header + 3 bands (vertical partition of the 400 px height). */
#define HDR_H     30
#define BAND1_Y   38
#define BAND1_H   98
#define BAND2_Y   144
#define BAND2_H   146
#define BAND3_Y   298
#define BAND3_H   98

/* ---- Palette (minimal: dark bg, light text, no accents) -------------------- */
#define COL_BG    0x000000
#define COL_HDR   0x12161C
#define COL_TXT   0xD2D6DC
#define COL_DIM   0x808890

/* 1 Hz refresh tick drives the dynamic lines. */
#define UI_REFRESH_MS  1000U

typedef struct
{
    lv_obj_t *b2_usb;
    lv_obj_t *b2_font;
    lv_obj_t *b2_freq;
    lv_obj_t *b2_uptime;
    lv_obj_t *b2_cache;
    lv_obj_t *b3_l1;
    lv_obj_t *b3_l2;
    lv_obj_t *b3_l3;
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

static lv_obj_t *make_band(lv_obj_t *parent, lv_coord_t y, lv_coord_t h,
                           const char *title)
{
    lv_obj_t *card = lv_obj_create(parent);

    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, UI_W - (2 * UI_PAD), h);
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
    lv_label_set_text_fmt(s_ui.b2_usb, "USB 状态  %s", usb_state_str(g_usb_state));
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
    lv_label_set_text(s_ui.b2_font, status);
}

static void refresh_runtime(void)
{
    uint32_t hits = 0U;
    uint32_t miss = 0U;

    lv_label_set_text_fmt(s_ui.b2_uptime, "运行  %02d:%02d:%02d",
                          (int)(s_uptime_sec / 3600U),
                          (int)((s_uptime_sec / 60U) % 60U),
                          (int)(s_uptime_sec % 60U));

    lv_font_gbk_cache_stats(&hits, &miss);
    lv_label_set_text_fmt(s_ui.b2_cache, "缓存  命中 %d / 读卡 %d",
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
/* Frame builder (called once)                                                */
/*----------------------------------------------------------------------------*/
static void ui_build_frame(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_t *hdr;
    lv_obj_t *b1;
    lv_obj_t *b2;
    lv_obj_t *b3;
    LCD_INFO *info;

    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr, 0, LV_PART_MAIN);

    /* Top header bar with the application title. */
    hdr = lv_obj_create(scr);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, UI_W, HDR_H);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(COL_HDR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, LV_PART_MAIN);
    (void)mk_label_center(hdr, 3, &lv_font_gbk_24, COL_TXT, "STM32F429 信息面板");

    /* Band 1 - 系统初始化 (hardware init summary). */
    b1 = make_band(scr, BAND1_Y, BAND1_H, "系统初始化");
    info = get_lcd_info();
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "LCD 控制器  ID = 0x%04X",
                 (unsigned int)((info != NULL) ? info->lcd_id : 0U));
        (void)mk_label(b1, 8, 32, &lv_font_gbk_16, COL_TXT, buf);
        (void)mk_label(b1, 8, 56, &lv_font_gbk_16, COL_TXT, "USB 主机  已初始化");
        (void)mk_label(b1, 8, 80, &lv_font_gbk_16, COL_DIM, "LVGL 渲染  已就绪");
    }

    /* Band 2 - 运行信息 (app_ui_create content). */
    b2 = make_band(scr, BAND2_Y, BAND2_H, "运行信息");
    s_ui.b2_usb   = mk_label(b2, 8, 32,  &lv_font_gbk_16, COL_TXT, "USB 状态  --");
    s_ui.b2_font  = mk_label(b2, 8, 56,  &lv_font_gbk_16, COL_TXT, "字库  --");
    s_ui.b2_freq  = mk_label(b2, 8, 80,  &lv_font_gbk_16, COL_TXT, "");
    lv_label_set_text_fmt(s_ui.b2_freq, "主频  %d MHz",
                          (int)(HAL_RCC_GetSysClockFreq() / 1000000U));
    s_ui.b2_uptime = mk_label(b2, 8, 104, &lv_font_gbk_16, COL_TXT, "运行  00:00:00");
    s_ui.b2_cache  = mk_label(b2, 8, 128, &lv_font_gbk_16, COL_DIM, "缓存  命中 0 / 读卡 0");

    /* Band 3 - 故障/消息 (app_ui_show_fault content). */
    b3 = make_band(scr, BAND3_Y, BAND3_H, "故障 / 消息");
    s_ui.b3_l1 = mk_label(b3, 8, 30, &lv_font_gbk_16, COL_TXT, "系统正常");
    s_ui.b3_l2 = mk_label(b3, 8, 54, &lv_font_gbk_16, COL_TXT, "");
    s_ui.b3_l3 = mk_label(b3, 8, 78, &lv_font_gbk_16, COL_TXT, "");

    s_built = 1U;
}

/*----------------------------------------------------------------------------*/
/* Public API                                                                 */
/*----------------------------------------------------------------------------*/
void app_ui_create(void)
{
    if (s_built == 0U)
    {
        ui_build_frame();
    }

    /* Normal operation: clear any previous fault text in band 3. */
    lv_label_set_text(s_ui.b3_l1, "系统正常");
    lv_label_set_text(s_ui.b3_l2, "");
    lv_label_set_text(s_ui.b3_l3, "");

    s_uptime_sec = 0U;
    s_refresh_req = 1U;   /* re-read font status on first tick */

    refresh_usb();
    refresh_font();
    refresh_runtime();

    (void)lv_timer_create(ui_tick_cb, UI_REFRESH_MS, NULL);
}

void app_ui_show_fault(const char *line1, const char *line2, const char *line3)
{
    if (s_built == 0U)
    {
        ui_build_frame();
    }

    lv_label_set_text(s_ui.b3_l1, (line1 != NULL && line1[0] != '\0') ? line1 : "");
    lv_label_set_text(s_ui.b3_l2, (line2 != NULL && line2[0] != '\0') ? line2 : "");
    lv_label_set_text(s_ui.b3_l3, (line3 != NULL && line3[0] != '\0') ? line3 : "");
}

void app_ui_request_usb_refresh(void)
{
    if (s_built != 0U)
    {
        s_refresh_req = 1U;
    }
}
