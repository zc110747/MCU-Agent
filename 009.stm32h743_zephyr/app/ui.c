/**
  ******************************************************************************
  * @file    ui.c
  * @brief   LVGL info screen - plain dark card style.
  *
  *   Black background, dark cards, white text only.  No accent colours,
  *   no status colour switching, no dots: keep it readable and neutral.
  ******************************************************************************
  */
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include "lv_port_font.h"
#include "ui.h"

/* ---- Palette ------------------------------------------------------------ */
#define CLR_BG       lv_color_black()                  /* 纯黑背景          */
#define CLR_CARD     lv_color_make(0x14, 0x18, 0x24)   /* 深色卡片（比底略亮）*/
#define CLR_CARD_BD  lv_color_make(0x2C, 0x34, 0x48)   /* 卡片描边          */
#define CLR_TEXT     lv_color_white()                  /* 白色文字          */

/* Mutable widgets updated at runtime. */
static lv_obj_t *label_uptime;
static lv_obj_t *label_font;
static lv_obj_t *label_led;

/* ---- Helpers ------------------------------------------------------------ */

/* One plain dark card (no dot, no accent). */
static lv_obj_t *make_card(lv_obj_t *parent, lv_coord_t y)
{
    lv_obj_t *card = lv_obj_create(parent);

    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 228, 30);
    lv_obj_set_pos(card, 6, y);
    lv_obj_set_style_bg_color(card, CLR_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, CLR_CARD_BD, 0);

    lv_obj_t *l = lv_label_create(card);
    lv_obj_set_style_text_font(l, &gbk_font_16, 0);
    lv_obj_set_style_text_color(l, CLR_TEXT, 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 12, 0);
    return l;
}

/* ---- Public API --------------------------------------------------------- */

void ui_show(const char *zephyr_ver, const char *lvgl_ver, uint32_t sys_clock_hz)
{
    char buf[64];
    lv_obj_t *scr = lv_scr_act();

    /* Plain black background. */
    lv_obj_set_style_bg_color(scr, CLR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Title bar (dark card, centred white text). */
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 228, 34);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_style_bg_color(bar, CLR_CARD, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 8, 0);

    lv_obj_t *title = lv_label_create(bar);
    lv_obj_set_style_text_font(title, &gbk_font_16, 0);
    lv_obj_set_style_text_color(title, CLR_TEXT, 0);
    lv_label_set_text(title, "STM32H743 中文显示");
    lv_obj_center(title);

    /* Versions. */
    lv_obj_t *l1 = make_card(scr, 46);
    snprintf(buf, sizeof(buf), "Zephyr %s | LVGL %s", zephyr_ver, lvgl_ver);
    lv_label_set_text(l1, buf);

    /* Core clock. */
    lv_obj_t *l2 = make_card(scr, 82);
    snprintf(buf, sizeof(buf), "主频 %u MHz", (unsigned)(sys_clock_hz / 1000000U));
    lv_label_set_text(l2, buf);

    /* Uptime (refreshed from main.c). */
    label_uptime = make_card(scr, 118);
    lv_label_set_text(label_uptime, "运行 0s");

    /* Font status. */
    label_font = make_card(scr, 154);
    lv_label_set_text(label_font, "字库 未加载");

    /* LED status. */
    label_led = make_card(scr, 190);
    lv_label_set_text(label_led, "LED 关");

    /* Bottom hint. */
    lv_obj_t *hint = lv_label_create(scr);
    lv_obj_set_style_text_font(hint, &gbk_font_16, 0);
    lv_obj_set_style_text_color(hint, CLR_TEXT, 0);
    lv_label_set_text(hint, "240x240 ST7789 SPI6");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -3);
}

void ui_set_font_status(const char *status)
{
    if (label_font != NULL && status != NULL)
    {
        lv_label_set_text(label_font, status);
    }
}

void ui_update_uptime(uint32_t uptime_ms)
{
    char buf[32];

    if (label_uptime == NULL)
    {
        return;
    }

    uint32_t sec = uptime_ms / 1000U;
    if (sec >= 3600U)
    {
        snprintf(buf, sizeof(buf), "运行 %uh%02um",
                 (unsigned)(sec / 3600U), (unsigned)((sec % 3600U) / 60U));
    }
    else if (sec >= 60U)
    {
        snprintf(buf, sizeof(buf), "运行 %um%02us",
                 (unsigned)(sec / 60U), (unsigned)(sec % 60U));
    }
    else
    {
        snprintf(buf, sizeof(buf), "运行 %us", (unsigned)sec);
    }
    lv_label_set_text(label_uptime, buf);
}

void ui_set_led(bool on)
{
    if (label_led != NULL)
    {
        lv_label_set_text(label_led, on ? "LED 心跳中" : "LED 关");
    }
}
