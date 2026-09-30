/**
  ******************************************************************************
  * @file    lv_font_gbk.c
  * @brief   LVGL fonts backed by the GBK glyph files on the SD card.
  *
  *  The three bitmap layouts involved
  *  ---------------------------------
  *  1. GBKxx.FON on the SD card : MSB first, COLUMN scan, (H+7)/8 bytes per
  *                                column.
  *  2. ASCII_xxxx_Table in flash: LSB first, ROW scan, every row padded up to a
  *                                whole number of bytes.
  *  3. What LVGL wants          : MSB first, ROW scan, one CONTINUOUS bit
  *                                stream - rows are NOT byte aligned.
  *                                (see width_bit = box_w * bpp in
  *                                 lv_draw_sw_letter.c :: draw_letter_normal)
  *
  *  Both sources therefore need a conversion pass.  For the SD glyphs we read
  *  the file verbatim (lcd_driver_get_hzmat_raw) and go straight from layout 1
  *  to layout 3 - transposing through the driver's LSB/row format first would
  *  cost a second pass for nothing.
  *
  *  Why the cache matters
  *  ---------------------
  *  Every miss is an f_lseek + f_read on the SD card, which is orders of
  *  magnitude slower than the conversion itself.  Without a cache a once-a-
  *  second clock update would re-read its glyphs from the card every time.  A
  *  small per-size LRU keeps the working set (the characters actually on
  *  screen) in RAM, so steady-state redraws touch the card zero times.
  ******************************************************************************
  */
#include "lv_font_gbk.h"
#include "lv_gbk_map.h"
#include "drv_oled_text.h"
#include "drv_oled_fonts.h"
#include <string.h>

/* ASCII tables cover 0x20..0x7E, in that order, starting at index 0 */
#define ASCII_FIRST     0x20u
#define ASCII_LAST      0x7Eu

/* Raw glyph straight off the card: 32x32 needs 128 bytes */
#define RAW_MAX_BYTES   128u

/*---------------------------------------------------------------------------*/
/* Cache                                                                      */
/*---------------------------------------------------------------------------*/

typedef struct
{
    uint16_t gbk;       /* GBK code held in this slot, 0 = empty */
    uint16_t age;       /* value of dsc->clock when last used    */
} gbk_slot_t;

typedef struct
{
    pFONT       *cjk;           /* SD backed descriptor, selects GBKxx.FON    */
    const pFONT *ascii;         /* compiled-in ASCII table                    */

    uint16_t     cjk_bytes;     /* LVGL bitmap size of one CJK glyph          */
    uint16_t     ascii_bytes;   /* LVGL bitmap size of one ASCII glyph        */

    gbk_slot_t  *slots;         /* cache_len entries                          */
    uint8_t     *cache_bmp;     /* cache_len * cjk_bytes                      */
    uint8_t      cache_len;

    uint8_t     *ascii_bmp;     /* ascii_bytes of scratch                     */

    uint16_t     clock;         /* monotonic counter driving the LRU          */
} gbk_font_dsc_t;

static uint32_t s_cache_hits;
static uint32_t s_cache_misses;

/*---------------------------------------------------------------------------*/
/* Bitmap conversion                                                          */
/*---------------------------------------------------------------------------*/

/**
  * @brief  MSB + column scan (GBKxx.FON)  ->  MSB + continuous row bit stream.
  */
static void conv_col_msb_to_lvgl(const uint8_t *src, uint8_t *dst,
                                 uint16_t w, uint16_t h)
{
    uint16_t bytes_per_col = (uint16_t)((h + 7u) / 8u);
    uint32_t idx = 0;
    uint16_t row, col;

    memset(dst, 0, ((uint32_t)w * h + 7u) / 8u);

    for (row = 0; row < h; row++)
    {
        const uint8_t *p    = src + (row >> 3);
        uint8_t        mask = (uint8_t)(0x80u >> (row & 7u));

        for (col = 0; col < w; col++, idx++)
        {
            if (p[(uint32_t)col * bytes_per_col] & mask)
            {
                dst[idx >> 3] |= (uint8_t)(0x80u >> (idx & 7u));
            }
        }
    }
}

/**
  * @brief  LSB + byte aligned rows (ASCII tables) -> MSB + continuous stream.
  */
static void conv_row_lsb_to_lvgl(const uint8_t *src, uint8_t *dst,
                                 uint16_t w, uint16_t h)
{
    uint16_t stride = (uint16_t)((w + 7u) / 8u);
    uint32_t idx = 0;
    uint16_t row, col;

    memset(dst, 0, ((uint32_t)w * h + 7u) / 8u);

    for (row = 0; row < h; row++)
    {
        const uint8_t *rp = src + (uint32_t)row * stride;

        for (col = 0; col < w; col++, idx++)
        {
            if (rp[col >> 3] & (uint8_t)(1u << (col & 7u)))
            {
                dst[idx >> 3] |= (uint8_t)(0x80u >> (idx & 7u));
            }
        }
    }
}

/*---------------------------------------------------------------------------*/
/* Glyph lookup                                                               */
/*---------------------------------------------------------------------------*/

/**
  * @brief  Return the cached LVGL bitmap of a GBK glyph, loading it from the
  *         SD card on a miss.
  */
static const uint8_t *cjk_glyph(gbk_font_dsc_t *d, uint16_t gbk)
{
    static uint8_t raw[RAW_MAX_BYTES];

    uint8_t  code[2];
    uint8_t  i;
    uint8_t  victim = 0;
    uint16_t oldest;

    d->clock++;

    /* Hit? */
    for (i = 0; i < d->cache_len; i++)
    {
        if (d->slots[i].gbk == gbk)
        {
            d->slots[i].age = d->clock;
            s_cache_hits++;
            return d->cache_bmp + (uint32_t)i * d->cjk_bytes;
        }
    }

    /* Miss: prefer a free slot, otherwise evict the least recently used one.
     * d->clock is free running and wraps, so rank by distance from "now"
     * instead of by the raw age value. */
    victim = 0;
    oldest = 0;
    for (i = 0; i < d->cache_len; i++)
    {
        uint16_t dist;

        if (d->slots[i].gbk == 0u)
        {
            victim = i;
            break;
        }

        dist = (uint16_t)(d->clock - d->slots[i].age);
        if (dist >= oldest)
        {
            oldest = dist;
            victim = i;
        }
    }

    s_cache_misses++;

    code[0] = (uint8_t)(gbk >> 8);
    code[1] = (uint8_t)(gbk & 0xFFu);

    if (lcd_driver_get_hzmat_raw(code, raw, d->cjk) != RT_OK)
    {
        /* Card missing or glyph out of range: render a blank box and leave the
         * cache untouched, so a retry after a remount can still succeed.
         * raw[] is RAW_MAX_BYTES == the largest cjk_bytes we ever ask for. */
        memset(raw, 0, d->cjk_bytes);
        return raw;
    }

    conv_col_msb_to_lvgl(raw,
                         d->cache_bmp + (uint32_t)victim * d->cjk_bytes,
                         d->cjk->Width, d->cjk->Height);

    d->slots[victim].gbk = gbk;
    d->slots[victim].age = d->clock;

    return d->cache_bmp + (uint32_t)victim * d->cjk_bytes;
}

/**
  * @brief  LVGL callback: describe one glyph (no file access here).
  */
static bool gbk_get_glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc_out,
                              uint32_t letter, uint32_t letter_next)
{
    gbk_font_dsc_t *d = (gbk_font_dsc_t *)font->dsc;
    uint16_t w, h;

    LV_UNUSED(letter_next);

    if (letter >= ASCII_FIRST && letter <= ASCII_LAST)
    {
        w = d->ascii->Width;
        h = d->ascii->Height;
    }
    else if (lv_gbk_from_unicode(letter) != 0u)
    {
        w = d->cjk->Width;
        h = d->cjk->Height;
    }
    else
    {
        return false;
    }

    dsc_out->adv_w          = w;    /* whole pixels: lv_font_fmt_txt.c already
                                     * folds its 1/16 px fixed point away      */
    dsc_out->box_w          = w;
    dsc_out->box_h          = h;
    dsc_out->ofs_x          = 0;
    dsc_out->ofs_y          = 0;    /* glyph box sits on the baseline          */
    dsc_out->bpp            = 1;
    dsc_out->is_placeholder = 0;

    return true;
}

/**
  * @brief  LVGL callback: hand out the glyph bitmap.
  * @note   The returned pointer only has to stay valid until the letter has
  *         been blitted, and lv_draw_sw_letter() does that immediately.
  */
static const uint8_t *gbk_get_glyph_bitmap(const lv_font_t *font, uint32_t letter)
{
    gbk_font_dsc_t *d = (gbk_font_dsc_t *)font->dsc;
    uint16_t gbk;

    if (letter >= ASCII_FIRST && letter <= ASCII_LAST)
    {
        const uint8_t *src = d->ascii->pTable +
                             (uint32_t)(letter - ASCII_FIRST) * d->ascii->Sizes;

        conv_row_lsb_to_lvgl(src, d->ascii_bmp,
                             d->ascii->Width, d->ascii->Height);
        return d->ascii_bmp;
    }

    gbk = lv_gbk_from_unicode(letter);
    if (gbk == 0u)
    {
        return NULL;
    }

    return cjk_glyph(d, gbk);
}

/*---------------------------------------------------------------------------*/
/* Font instances                                                             */
/*---------------------------------------------------------------------------*/

/* Bitmap sizes are ceil(w * h / 8) because LVGL packs the rows continuously */
#define LVGL_BMP_BYTES(w, h)    (((w) * (h) + 7u) / 8u)

#define GBK_FONT_STORAGE(sz, aw, cache_n)                                       \
    static gbk_slot_t s_slots_##sz[cache_n];                                    \
    static uint8_t    s_cache_##sz[(cache_n) * LVGL_BMP_BYTES(sz, sz)];         \
    static uint8_t    s_ascii_bmp_##sz[LVGL_BMP_BYTES(aw, sz)]

/* cache depth chosen per size: 16 px is the workhorse and gets the most slots,
 * 32 px costs 128 B a glyph so it gets the fewest. */
GBK_FONT_STORAGE(12,  6, 24);
GBK_FONT_STORAGE(16,  8, 48);
GBK_FONT_STORAGE(24, 12, 32);
GBK_FONT_STORAGE(32, 16, 24);

#define GBK_FONT_DEFINE(sz, aw, cache_n)                                        \
    static gbk_font_dsc_t s_dsc_##sz = {                                        \
        .cjk         = &CH_TEXT_Font##sz,                                       \
        .ascii       = &ASCII_Font##sz,                                         \
        .cjk_bytes   = LVGL_BMP_BYTES(sz, sz),                                  \
        .ascii_bytes = LVGL_BMP_BYTES(aw, sz),                                  \
        .slots       = s_slots_##sz,                                            \
        .cache_bmp   = s_cache_##sz,                                            \
        .cache_len   = (cache_n),                                               \
        .ascii_bmp   = s_ascii_bmp_##sz,                                        \
        .clock       = 0,                                                       \
    };                                                                          \
    const lv_font_t lv_font_gbk_##sz = {                                        \
        .get_glyph_dsc       = gbk_get_glyph_dsc,                               \
        .get_glyph_bitmap    = gbk_get_glyph_bitmap,                            \
        .line_height         = (sz) + 2,                                        \
        .base_line           = 2,                                               \
        .subpx               = LV_FONT_SUBPX_NONE,                              \
        .underline_position  = -1,                                              \
        .underline_thickness = 1,                                               \
        .dsc                 = &s_dsc_##sz,                                     \
        .fallback            = NULL,                                            \
    }

GBK_FONT_DEFINE(12,  6, 24);
GBK_FONT_DEFINE(16,  8, 48);
GBK_FONT_DEFINE(24, 12, 32);
GBK_FONT_DEFINE(32, 16, 24);

/*---------------------------------------------------------------------------*/
/* Housekeeping                                                               */
/*---------------------------------------------------------------------------*/

void lv_font_gbk_reset_cache(void)
{
    gbk_font_dsc_t *all[] = { &s_dsc_12, &s_dsc_16, &s_dsc_24, &s_dsc_32 };
    uint32_t i;

    for (i = 0; i < sizeof(all) / sizeof(all[0]); i++)
    {
        memset(all[i]->slots, 0, (size_t)all[i]->cache_len * sizeof(gbk_slot_t));
        all[i]->clock = 0;
    }

    s_cache_hits   = 0;
    s_cache_misses = 0;
}

void lv_font_gbk_cache_stats(uint32_t *hits, uint32_t *misses)
{
    if (hits != NULL)
    {
        *hits = s_cache_hits;
    }
    if (misses != NULL)
    {
        *misses = s_cache_misses;
    }
}
