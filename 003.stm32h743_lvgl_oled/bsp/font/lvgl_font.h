/**
  ******************************************************************************
  * @file    lvgl_font.h
  * @brief   LVGL font backed by a CTF index plus the original TTF.
  *
  *  Pipeline
  *  --------
  *      LVGL measures a character
  *          -> ctf_get_glyph_dsc()   CTF lookup only, no TTF access at all
  *      LVGL draws a character
  *          -> ctf_get_glyph_bitmap() bitmap cache
  *                  | miss
  *                  v
  *             stb_adapter_render()  -> ttf_reader -> 16 KB block cache -> SD
  *
  *  Missing glyphs
  *  --------------
  *  A code point the index does not carry is *not* an error.  ctf_get_glyph_dsc()
  *  returns false, LVGL walks font->fallback and renders the character from the
  *  built-in Montserrat bitmap font, which is compiled into flash.  English and
  *  digits therefore keep working even when the index is missing or unreadable.
  *  For a character that no font in the chain has - a rare CJK ideograph, say -
  *  LVGL draws nothing at all (LV_USE_FONT_PLACEHOLDER is 0), so the text simply
  *  closes up instead of showing placeholder boxes.
  *
  *  Nothing is logged on a miss and the TTF is never touched for one.
  ******************************************************************************
  */
#ifndef __LVGL_FONT_H
#define __LVGL_FONT_H

#include <stdint.h>
#include "lvgl.h"
#include "main.h"
#include "ctf_reader.h"   /* ctf_resident_t, reported by lvgl_font_get_resident */

/** Sizes this backend instantiates.  Add pairs in lvgl_font.c only. */
#define CTF_FONT_12   12u
#define CTF_FONT_16   16u
#define CTF_FONT_24   24u
#define CTF_FONT_32   32u
#define CTF_FONT_SIZES 4u

/**
  * @brief  Bring up the engine: open the index, verify the TTF, parse it.
  * @param  ctf_path  e.g. "1:/SYSTEM/HarmonyOS_Sans_SC/HarmonyOS_Sans_SC_Regular.ctf"
  * @param  ttf_path  the .ttf the index was generated from
  * @retval RT_OK when at least one size is usable
  */
GlobalType_t lvgl_font_engine_init(const char *ctf_path, const char *ttf_path);

/** Tear everything down and release the SD card handles. */
void lvgl_font_engine_deinit(void);

/** 1 when the index and the TTF are both loaded. */
int lvgl_font_engine_ready(void);

/**
  * @brief  The font for @p px_size pixels.
  * @retval the closest supported size, or NULL before init
  */
const lv_font_t *lvgl_font_get(uint16_t px_size);

/** Paths currently open, for the boot banner. */
const char *lvgl_font_ctf_path(void);
const char *lvgl_font_ttf_path(void);

/*---------------------------------------------------------------------------*/
/* Runtime statistics - the acceptance bench reads these                      */
/*---------------------------------------------------------------------------*/

typedef struct
{
    uint32_t lookups;        /**< get_glyph_dsc + get_glyph_bitmap calls     */
    uint32_t missing;        /**< code points the index does not carry        */
    uint32_t bmp_hits;
    uint32_t bmp_misses;
    uint32_t bmp_flushes;    /**< bitmap cache wrapped                        */
    uint32_t bmp_bytes;      /**< bytes of glyph bitmaps held                 */
    uint32_t arena_peak;     /**< high-water mark of stb's scratch arena      */
    uint32_t arena_fails;    /**< glyphs that did not fit in that arena       */
    uint32_t ttf_hits;       /**< block cache hits on the TTF                 */
    uint32_t ttf_misses;     /**< block cache misses -> real SD reads         */
    uint32_t ttf_fills;      /**< f_read() calls issued                       */
    uint32_t ttf_bytes;      /**< bytes pulled off the card                   */
    uint32_t ttf_seek_us;    /**< microseconds spent in f_lseek(), cumulative  */
    uint32_t ttf_read_us;    /**< microseconds spent in f_read(), cumulative   */
    uint32_t ctf_lookups;
    uint32_t ctf_not_found;
    uint32_t ctf_io_errors;
    uint32_t ctf_page_ram;   /**< page records served from the resident table  */
    uint32_t ctf_page_sd;    /**< page records that still went to the card     */
} lvgl_font_stats_t;

/*---------------------------------------------------------------------------*/
/* Resident index footprint                                                  */
/*---------------------------------------------------------------------------*/

/**
  * @brief  What the index pinned in RAM at init, and how much it cost.
  *
  *  Valid after lvgl_font_engine_init(); zeroed before it.  `page_resident == 1`
  *  is the interesting bit: it means the second hop of every lookup - and so the
  *  whole NOT_FOUND decision - is a pure RAM operation.
  */
void lvgl_font_get_resident(ctf_resident_t *out);

/** Bytes of RAM this backend holds for the index (table dir + L1 + pages). */
uint32_t lvgl_font_resident_bytes(void);

/** Bytes reserved for the resident page table, whether or not it got used. */
uint32_t lvgl_font_page_pool_bytes(void);

/*---------------------------------------------------------------------------*/
/* On-target acceptance probe                                                */
/*---------------------------------------------------------------------------*/

/**
  * @brief  One probed code point.
  *
  *  The invariant the acceptance bench checks is `sd_reads == 0` for every
  *  entry with `found == 0`: a character the index does not carry must cost
  *  nothing at all, because it never reaches the TTF.  Which code points end
  *  up in which bucket depends on the font, so the probe reports rather than
  *  assumes.
  */
typedef struct
{
    uint32_t cp;          /**< code point under test                         */
    uint16_t px;          /**< requested size                                 */
    uint8_t  found;       /**< 1 = index hit, 0 = NOT_FOUND                   */
    uint8_t  empty;       /**< 1 = EMPTY glyph: has an advance, no outline    */
    uint16_t adv_w;       /**< advance in pixels                              */
    uint16_t box_w;       /**< bitmap width                                   */
    uint16_t box_h;       /**< bitmap height                                  */
    int16_t  ofs_x;       /**< horizontal offset from the pen                 */
    int16_t  ofs_y;       /**< vertical offset from the baseline              */
    uint16_t fb_adv_w;    /**< advance the built-in fallback would give       */
    uint32_t cold_us;     /**< first rasterisation, microseconds               */
    uint32_t warm_us;     /**< second call: bitmap cache hit                  */
    uint32_t sd_us;       /**< of cold_us, the part spent waiting on the card */
    uint32_t ink;         /**< non-zero pixels; 0 means nothing was drawn      */
    uint32_t sd_reads;    /**< f_read() calls this code point caused           */
} lvgl_font_probe_t;

/**
  * @brief  Rasterise a fixed vector of code points and report what it cost.
  *
  *  Flushes the glyph cache first, so every entry is timed cold and then warm.
  *  Safe to call any time after lvgl_font_engine_init(); it only reads.
  *
  * @param  out       caller-supplied array
  * @param  capacity  number of elements in @p out
  * @return number of entries written
  */
uint32_t lvgl_font_selftest(lvgl_font_probe_t *out, uint32_t capacity);

void lvgl_font_get_stats(lvgl_font_stats_t *out);
void lvgl_font_reset_stats(void);

/** Drop cached glyph bitmaps; the next draw re-rasterises them. */
void lvgl_font_flush_bitmaps(void);

/*---------------------------------------------------------------------------*/
/* Glyph preload + page pinning                                               */
/*---------------------------------------------------------------------------*/

/** @return the pixel size a CTF/TTF font was built at, or 0 if @p f is not one
  *  (e.g. a GBK or Montserrat font) - used to decide whether to preload. */
uint16_t lvgl_font_px_of(const lv_font_t *f);

/** Scan @p text, dedupe its CJK code points, and queue the missing ones for
  *  asynchronous rasterisation into the glyph cache.  No-op for non-TTF engines
  *  (caller should not invoke it, but it is safe if it does). */
void lvgl_font_preload_text(const char *text, uint16_t px);

/** Convenience: preload the text of an LVGL label at its current font size. */
void lvgl_font_preload_label(const lv_obj_t *lbl);

/** @return number of code points still queued for asynchronous rasterisation
  *         (0 when the engine is not CTF/TTF or the queue has drained). */
uint32_t lvgl_font_preload_pending(void);

/** Call when a page becomes visible: bumps the glyph-cache epoch so the old
  *  page's glyphs become LRU-evictable while the new page stays pinned. */
void lvgl_font_on_page_shown(void);

#endif /* __LVGL_FONT_H */
