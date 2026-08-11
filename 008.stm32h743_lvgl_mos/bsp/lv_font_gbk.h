/**
  ******************************************************************************
  * @file    lv_font_gbk.h
  * @brief   LVGL fonts backed by the GBK glyph files on the SD card.
  *
  *  Four sizes are exported.  Each of them serves ASCII from the compiled-in
  *  tables in drv_oled_fonts.c and every other character from
  *  1:/SYSTEM/FONT/GBKxx.FON, so a full Chinese font costs no flash at all.
  *
  *      font              CJK box    ASCII box   glyph file
  *      ----------------  ---------  ----------  ------------
  *      lv_font_gbk_12    12 x 12     6 x 12     GBK12.FON
  *      lv_font_gbk_16    16 x 16     8 x 16     GBK16.FON
  *      lv_font_gbk_24    24 x 24    12 x 24     GBK24.FON
  *      lv_font_gbk_32    32 x 32    16 x 32     GBK32.FON
  ******************************************************************************
  */
#ifndef __LV_FONT_GBK_H
#define __LV_FONT_GBK_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

LV_FONT_DECLARE(lv_font_gbk_12)
LV_FONT_DECLARE(lv_font_gbk_16)
LV_FONT_DECLARE(lv_font_gbk_24)
LV_FONT_DECLARE(lv_font_gbk_32)

/**
  * @brief  Drop every cached glyph.
  * @note   The caches start out empty, so this is only needed after the SD card
  *         has been remounted or the font files have been replaced.
  */
void lv_font_gbk_reset_cache(void);

/**
  * @brief  Glyph cache counters, handy when profiling the SD traffic.
  * @param  hits    number of glyphs served from RAM      (may be NULL)
  * @param  misses  number of glyphs that hit the SD card (may be NULL)
  */
void lv_font_gbk_cache_stats(uint32_t *hits, uint32_t *misses);

#ifdef __cplusplus
}
#endif

#endif /* __LV_FONT_GBK_H */
