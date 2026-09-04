/**
  ******************************************************************************
  * @file    glyph_cache.h
  * @brief   Rasterised-glyph cache for the TTF / CTF font backend.
  *
  *  Role
  *  -----
  *  Once a glyph has been rasterised from the TTF it stays here as an 8-bpp
  *  alpha bitmap, keyed by (unicode, px).  The next time LVGL needs the same
  *  code point it is returned straight from RAM - no stb parse, no SD read.
  *
 *  Layout
 *  ------
 *  A single 200 KB pool (placed in RAM_D2 by the attribute in glyph_cache.c)
 *  holds the pixel data.  Entries are variable length (w * h bytes, 4-aligned)
 *  so the pool is managed by a tiny boundary-tag-free heap with coalescing.
  *  That lets us honour "reclaim the space of an evicted glyph" without the
  *  waste a fixed-slot scheme would impose at 12 px vs 32 px.
  *
  *  LRU + page pinning
  *  -------------------
  *  Each entry carries an access stamp (lru) and a page epoch.  Eviction walks
  *  the table for the least-recently-used entry whose epoch is NOT the current
  *  one and frees it, until the request fits.  A glyph used on the page that is
  *  currently on screen is promoted to the current epoch on every hit, so it is
  *  never evicted - which is exactly "淘汰不得影响正在使用或当前页面固定字符".
  *  Switching pages bumps the epoch; the old page's glyphs fall behind and are
  *  reclaimed by LRU under pressure while the new page stays pinned.
  ******************************************************************************
  */
#ifndef __GLYPH_CACHE_H
#define __GLYPH_CACHE_H

#include <stdint.h>

/** Capacity of the rasterised-glyph bitmap pool. */
#ifndef GLYPH_CACHE_CAPACITY
#define GLYPH_CACHE_CAPACITY  (200u * 1024u)
#endif

/** Max distinct glyph entries the table can track. */
#ifndef GLYPH_CACHE_MAX_ENTRIES
#define GLYPH_CACHE_MAX_ENTRIES  (640u)
#endif

void glyph_cache_init(void);
void glyph_cache_reset(void);  /**< drop every cached bitmap (keep counters) */

/**
  * @brief  Look up a glyph.  On hit, promotes it to the current epoch, refreshes
  *         its LRU stamp, fills w/h/bytes and returns the pixel buffer.
  * @retval pointer into the pool, or NULL on miss / not present.
  */
const uint8_t *glyph_cache_lookup(uint32_t unicode, uint16_t px,
                                  uint16_t *w, uint16_t *h, uint32_t *bytes);

/**
  * @brief  Reserve space for a freshly rasterised glyph and record it.
  *         Evicts LRU, non-current-epoch entries first if the pool is short.
  * @retval pointer to fill with w*h bytes, or NULL if it cannot be made to fit.
  */
uint8_t *glyph_cache_insert(uint32_t unicode, uint16_t px,
                            uint16_t w, uint16_t h, uint32_t *bytes);

/** Bump the page epoch; previously-displayed pages become evictable. */
void glyph_cache_bump_epoch(void);

void glyph_cache_stats(uint32_t *hits, uint32_t *misses, uint32_t *evicts,
                       uint32_t *used_bytes, uint32_t *entries);
void glyph_cache_reset_stats(void);

#endif /* __GLYPH_CACHE_H */
