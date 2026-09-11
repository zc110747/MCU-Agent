/**
  ******************************************************************************
  * @file    ui_page_fault.c
  * @brief   ASCII-only fault page - see ui_page_fault.h.
  ******************************************************************************
  */
#include "ui_page_fault.h"
#include "ui_common.h"
#include "lvgl.h"

void ui_page_fault_show(const char *line1, const char *line2, const char *line3)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_t *hdr;

    lv_obj_clean(scr);

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
    (void)ui_mk_label_center(hdr, 6, UI_FONT(16), 0xFFFFFF, "SD / FONT ERROR");

    (void)ui_mk_label(scr, UI_PAD, 50,  UI_FONT(16), 0xFFD966,
                      (line1 != NULL) ? line1 : "");
    (void)ui_mk_label(scr, UI_PAD, 74,  UI_FONT(16), 0xFFFFFF,
                      (line2 != NULL) ? line2 : "");
    (void)ui_mk_label(scr, UI_PAD, 98,  UI_FONT(16), 0xFFFFFF,
                      (line3 != NULL) ? line3 : "");

    (void)ui_mk_label(scr, UI_PAD, 140, UI_FONT(12), 0x80D0FF,
                      "Expected on the card:");
    (void)ui_mk_label(scr, UI_PAD, 158, UI_FONT(12), 0xB0B0B0,
                      "1:/SYSTEM/FONT/UNIGBK.BIN");
    (void)ui_mk_label(scr, UI_PAD, 174, UI_FONT(12), 0xB0B0B0,
                      "1:/SYSTEM/FONT/GBK12.FON");
    (void)ui_mk_label(scr, UI_PAD, 190, UI_FONT(12), 0xB0B0B0,
                      "1:/SYSTEM/FONT/GBK16.FON");
    (void)ui_mk_label(scr, UI_PAD, 206, UI_FONT(12), 0xB0B0B0,
                      "1:/SYSTEM/FONT/GBK24.FON");
    (void)ui_mk_label(scr, UI_PAD, 222, UI_FONT(12), 0xB0B0B0,
                      "1:/SYSTEM/FONT/GBK32.FON");
}
