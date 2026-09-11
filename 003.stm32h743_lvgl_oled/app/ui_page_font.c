/**
  ******************************************************************************
  * @file    ui_page_font.c
  * @brief   Font-engine status page - see ui_page_font.h.
  ******************************************************************************
  */
#include "ui_page_font.h"
#include "ui_common.h"
#include "lvgl.h"

lv_obj_t *ui_page_font_build(void)
{
    lv_obj_t *scr = ui_common_screen_create();
    lv_obj_t *hdr;

    hdr = lv_obj_create(scr);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, UI_W, HDR_H);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x0A5C3D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, LV_PART_MAIN);
    (void)ui_mk_label_center(hdr, 6, UI_FONT(16), COL_HDR_TXT, "鸿蒙字体引擎");

    (void)ui_mk_label(scr, UI_PAD, 44,  UI_FONT(16), COL_LABEL, "渲染链路  CTF 索引 + TTF");
    (void)ui_mk_label(scr, UI_PAD, 70,  UI_FONT(16), COL_LABEL, "默认字体  HarmonyOS SC");
    (void)ui_mk_label(scr, UI_PAD, 96,  UI_FONT(16), COL_LABEL, "支持字号  12/16/24/32");
    (void)ui_mk_label(scr, UI_PAD, 122, UI_FONT(16), COL_LABEL, "缺字处理  回退 Montserrat");
    (void)ui_mk_label(scr, UI_PAD, 148, UI_FONT(16), COL_VALUE, "本页用途  评估栅格化时延");
    (void)ui_mk_label(scr, UI_PAD, 174, UI_FONT(16), COL_VALUE, "切换节奏  每 5 秒自动翻页");
    (void)ui_mk_label(scr, UI_PAD, 200, UI_FONT(12), COL_DIM,   "冷启动首帧最慢 之后命中缓存");

    return scr;
}
