/**
  ******************************************************************************
  * @file    ui_common.c
  * @brief   Shared LVGL helpers - see ui_common.h.
  ******************************************************************************
  */
#include "ui_common.h"

lv_obj_t *ui_common_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);   /* top-level screen */

    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr, 0, LV_PART_MAIN);
    return scr;
}

lv_obj_t *ui_mk_label(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                      const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);

    lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(lbl, 0, LV_PART_MAIN);
    lv_label_set_text(lbl, text);
    lv_obj_set_pos(lbl, x, y);

    /* TTF glyph preload (no-op for GBK): scan this label's text and queue the
     * missing CJK code points for asynchronous rasterisation. */
    lv_font_provider_preload_label(lbl);

    return lbl;
}

lv_obj_t *ui_mk_label_center(lv_obj_t *parent, lv_coord_t y,
                             const lv_font_t *font, uint32_t color,
                             const char *text)
{
    lv_obj_t *lbl = ui_mk_label(parent, 0, y, font, color, text);

    /* Full-width label + centred text keeps the position stable when the
     * string length changes (e.g. 9:05 -> 10:05), which avoids repainting a
     * shifting box every second. */
    lv_obj_set_width(lbl, UI_W);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(lbl, 0, y);

    return lbl;
}

void ui_mk_separator(lv_obj_t *parent, lv_coord_t y)
{
    lv_obj_t *ln = lv_obj_create(parent);

    lv_obj_remove_style_all(ln);
    lv_obj_set_size(ln, UI_W - (2 * UI_PAD), 1);
    lv_obj_set_pos(ln, UI_PAD, y);
    lv_obj_set_style_bg_color(ln, lv_color_hex(COL_SEP), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ln, LV_OPA_COVER, LV_PART_MAIN);
}

void ui_align_right(lv_obj_t *lbl, lv_coord_t y)
{
    lv_obj_set_width(lbl, UI_W - (2 * UI_PAD));
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_pos(lbl, UI_PAD, y);
}
