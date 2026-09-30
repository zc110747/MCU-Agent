/**
 ******************************************************************************
 * @file    lv_font_harmony.h
 * @brief   LVGL fonts rendered from HarmonyOS Sans TC .ttf files on the SD card.
 *
 *  Files live in 1:/SYSTEM/HarmonyOS_Sans_TC/.  The directory is scanned at
 *  boot and the best looking weight is picked automatically, so the exact file
 *  names the card happens to carry do not matter (see lv_font_harmony.c for
 *  the weight preference order and for the listing printed over the UART).
 *
 *  Two caches sit between LVGL and the SD card:
 *
 *   1. Glyph descriptor cache (this file) - LVGL asks for adv_w/box_w/box_h on
 *      every layout and every draw pass.  Without a cache that is a full stb
 *      cmap + loca + glyf walk per character *per frame*; with it, a steady
 *      state redraw touches the card zero times.
 *   2. Glyph bitmap cache (LVGL's tiny_ttf, lv_mem backed LRU) - keeps the
 *      rasterised 8bpp anti-aliased bitmaps.
 *
 *  Kerning is deliberately not applied: the descriptor cache is keyed on a
 *  single code point, so every query is issued with letter_next = 0.  The UI is
 *  overwhelmingly CJK (no kerning) and the Latin it does show gains a stable,
 *  jitter free layout in exchange.
 *
 *  If a glyph is missing from the .ttf, LVGL falls through to ->fallback,
 *  which is wired to the matching GBK bitmap font.
 ******************************************************************************
 */
#ifndef __LV_FONT_HARMONY_H
#define __LV_FONT_HARMONY_H

#include "lvgl.h"
#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Directory scanned for *.ttf, and the sizes instantiated from it. */
#define HARMONY_FONT_DIR "1:/SYSTEM/HarmonyOS_Sans_TC"


/**
 * @brief  Glyph descriptor cache counters.
 * @param  hits    descriptors served from RAM        (may be NULL)
 * @param  misses  descriptors that reached the card  (may be NULL)
 */
void lv_font_harmony_stats(uint32_t *hits, uint32_t *misses);


#ifdef __cplusplus
}
#endif

#endif /* __LV_FONT_HARMONY_H */
