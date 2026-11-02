/**
 * @file    lv_font_gbk.h
 * @brief   LVGL fonts backed by the GBK glyph files on the SD card.
 * @note    document/stm32f429/video_player_port.md A8
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
