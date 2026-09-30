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

/* 启动页控件句柄 */
typedef struct
{
    lv_obj_t *bar;
    lv_obj_t *pct;
    lv_obj_t *status;
} boot_widgets_t;

static boot_widgets_t g_boot = {0};

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

    (void)ui_mk_label_center(scr, 96, UI_FONT(16), COL_DATE, "Waiting...");

    g_boot.bar = lv_bar_create(scr);
    lv_obj_remove_style_all(g_boot.bar);
    lv_obj_set_size(g_boot.bar, UI_W - (2 * UI_PAD), 14);
    lv_obj_set_pos(g_boot.bar, UI_PAD, 150);
    lv_bar_set_range(g_boot.bar, 0, 100);
    lv_bar_set_value(g_boot.bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_boot.bar, lv_color_hex(COL_BAR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_boot.bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(g_boot.bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_boot.bar, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(g_boot.bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_boot.bar, 4, LV_PART_INDICATOR);

    g_boot.pct    = ui_mk_label_center(scr, 176, UI_FONT(12), COL_DIM, "0%");
    g_boot.status = ui_mk_label_center(scr, 204, UI_FONT(12), COL_LABEL, "系统启动中...");

    return scr;
}

void ui_page_boot_set(uint8_t pct)
{
    if (g_boot.bar != NULL)
    {
        lv_bar_set_value(g_boot.bar, (int32_t)pct, LV_ANIM_OFF);
    }
    if (g_boot.pct != NULL)
    {
        lv_label_set_text_fmt(g_boot.pct, "%u%%", (unsigned)pct);
    }
}

void ui_page_boot_set_status(const char *status)
{
    if ((g_boot.status != NULL) && (status != NULL))
    {
        lv_label_set_text(g_boot.status, status);
    }
}
