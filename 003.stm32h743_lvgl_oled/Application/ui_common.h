/**
  ******************************************************************************
  * @file    ui_common.h
  * @brief   Shared LVGL helpers and visual constants for all UI pages.
  *
  *  Every page (info panel, font status, boot/loading, fault) is built from
  *  the same primitives so the look stays consistent and a label's text is
  *  glyph-preloaded transparently (TTF engines only) through
  *  lv_font_provider_preload_label().
  ******************************************************************************
  */
#ifndef __UI_COMMON_H
#define __UI_COMMON_H

#include "lvgl.h"
#include "lv_font_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Geometry -------------------------------------------------------------*/
#define UI_W      240
#define UI_H      240
#define UI_PAD    8
#define HDR_H     28

/* ---- Palette ---------------------------------------------------------------*/
#define COL_BG        0x000000
#define COL_HDR       0x0A3D62
#define COL_HDR_TXT   0xFFD966
#define COL_CLOCK     0x00E5FF
#define COL_DATE      0xFFFFFF
#define COL_LABEL     0x8A8A8A
#define COL_VALUE     0x40E070
#define COL_ACCENT    0xFFA000
#define COL_BAR_BG    0x2A2A2A
#define COL_SEP       0x243447
#define COL_DIM       0x606060
#define COL_ERR       0xFF4040

/** Font for a pixel size, never NULL (routed through the active engine). */
#define UI_FONT(px)   lv_font_provider_get((px))

/** Build a fresh top-level screen with the standard black background. */
lv_obj_t *ui_common_screen_create(void);

/** Plain label: transparent background, no padding, fixed position. */
lv_obj_t *ui_mk_label(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                      const lv_font_t *font, uint32_t color, const char *text);

/** Same, horizontally centred on the screen. */
lv_obj_t *ui_mk_label_center(lv_obj_t *parent, lv_coord_t y,
                             const lv_font_t *font, uint32_t color,
                             const char *text);

/** 1 px horizontal rule. */
void ui_mk_separator(lv_obj_t *parent, lv_coord_t y);

/** Right-align a label against the screen edge without a layout pass. */
void ui_align_right(lv_obj_t *lbl, lv_coord_t y);

#ifdef __cplusplus
}
#endif

#endif /* __UI_COMMON_H */
