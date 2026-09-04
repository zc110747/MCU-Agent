/**
  ******************************************************************************
  * @file    ui_page_boot.c
  * @brief   Boot / font-preload loading page - see ui_page_boot.h.
  *
  *  Shown first at power-on.  Displays "Waiting..." plus a progress bar that
  *  the orchestrator (app_ui.c) fills while the TTF glyphs are preloaded into
  *  the glyph cache.  For the GBK engine there is nothing to preload, so the
  *  bar simply animates across the mandatory minimum 2 s.
  ******************************************************************************
  */
#include "ui_page_boot.h"
#include "ui_common.h"
#include "lvgl.h"

static lv_obj_t *s_bar    = NULL;
static lv_obj_t *s_pct    = NULL;
static lv_obj_t *s_status = NULL;

lv_obj_t *ui_page_boot_build(void)
{
    lv_obj_t *scr = ui_common_screen_create();
    lv_obj_t *hdr;

    hdr = lv_obj_create(scr);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, UI_W, HDR_H);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(COL_HDR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, LV_PART_MAIN);
    (void)ui_mk_label_center(hdr, 6, UI_FONT(16), COL_HDR_TXT, "STM32H743");

    (void)ui_mk_label_center(scr, 96,  UI_FONT(16), COL_DATE, "Waiting...");

    s_bar = lv_bar_create(scr);
    lv_obj_remove_style_all(s_bar);
    lv_obj_set_size(s_bar, UI_W - (2 * UI_PAD), 14);
    lv_obj_set_pos(s_bar, UI_PAD, 150);
    lv_bar_set_range(s_bar, 0, 100);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(COL_BAR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar, 4, LV_PART_INDICATOR);

    s_pct = ui_mk_label_center(scr, 176, UI_FONT(12), COL_DIM, "0%");
    s_status = ui_mk_label_center(scr, 204, UI_FONT(12), COL_LABEL, "系统启动中...");

    return scr;
}

void ui_page_boot_set(uint8_t pct)
{
    if (s_bar != NULL)
    {
        lv_bar_set_value(s_bar, (int32_t)pct, LV_ANIM_OFF);
    }
    if (s_pct != NULL)
    {
        lv_label_set_text_fmt(s_pct, "%u%%", (unsigned)pct);
    }
}

void ui_page_boot_set_status(const char *status)
{
    if ((s_status != NULL) && (status != NULL))
    {
        lv_label_set_text(s_status, status);
    }
}
