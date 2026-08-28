/**
  ******************************************************************************
  * @file    emwin_font_gbk.h
  * @brief   Custom emWin fonts: UTF-8 -> GBK -> SD FON, with an on-demand
  *          glyph cache.  ASCII glyphs come from the compiled-in tables,
  *          Chinese glyphs are read on demand from the SD card.
  ******************************************************************************
  */
#ifndef EMWIN_FONT_GBK_H
#define EMWIN_FONT_GBK_H

#include "GUI.h"
#include "drv_oled_fonts.h"   /* pFONT, ASCII_Font*                       */
#include "drv_oled_text.h"     /* CH_TEXT_Font*, lcd_driver_get_hzmat      */
#include "lv_gbk_map.h"        /* lv_gbk_from_unicode                      */

#ifdef __cplusplus
extern "C" {
#endif

/* One object per pixel height, mirroring the 003 LVGL font set. */
extern const GUI_FONT EMWIN_FONT_GBK12;
extern const GUI_FONT EMWIN_FONT_GBK16;
extern const GUI_FONT EMWIN_FONT_GBK24;
extern const GUI_FONT EMWIN_FONT_GBK32;

/* Glyph cache hit / SD-read counters (for the "缓存" status line). */
void emwin_font_get_cache_stats(uint32_t * hit, uint32_t * read);

#ifdef __cplusplus
}
#endif

#endif /* EMWIN_FONT_GBK_H */
