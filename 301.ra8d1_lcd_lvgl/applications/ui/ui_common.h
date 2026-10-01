/**
 ******************************************************************************
 * @file    ui_common.h
 * @brief   Shared LVGL helpers and visual constants for all UI pages.
 *
 *  Ported 1:1 from the reference project 003.stm32h743_lvgl_oled (same
 *  primitive set, same palette, same call signatures) with the geometry
 *  rescaled from its 240x240 panel to this board's 480x360 MIPI panel.
 *  Every page (boot/loading, menu, system info) is built from these same
 *  primitives so the look stays consistent.
 ******************************************************************************
 */
#ifndef __UI_COMMON_H
#define __UI_COMMON_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Geometry -------------------------------------------------------------*/
#define UI_W 480
#define UI_H 360
#define UI_PAD 12
#define HDR_H 36

/* ---- Palette ---------------------------------------------------------------*/
#define COL_BG 0x000000
#define COL_HDR 0x0A3D62
#define COL_HDR_TXT 0xFFD966
#define COL_CLOCK 0x00E5FF
#define COL_DATE 0xFFFFFF
#define COL_LABEL 0x8A8A8A
#define COL_VALUE 0x40E070
#define COL_ACCENT 0xFFA000
#define COL_BAR_BG 0x2A2A2A
#define COL_SEP 0x243447
#define COL_DIM 0x606060
#define COL_ERR 0xFF4040
#define COL_SEL_BG 0x123A5C   /* selected menu row background */
#define COL_SEL_TXT 0xFFD966  /* selected menu row text       */

/** Font for a pixel size, never NULL. */
#define UI_FONT(px) ui_common_font(px)

/** Map a nominal pixel size to an enabled Montserrat face (largest <= px). */
const lv_font_t *ui_common_font(int px);

/** Build a fresh top-level screen with the standard black background. */
lv_obj_t *ui_common_screen_create(void);

/** Standard page header: title bar + centred title text. Returns the bar. */
lv_obj_t *ui_common_header(lv_obj_t *scr, const char *title);

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
