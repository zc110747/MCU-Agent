/**
  ******************************************************************************
  * @file    lv_port_font.c
  * @brief   LVGL v8 custom font: Chinese (GBK) glyphs streamed from the SD card.
  *
  *  Pipeline for every glyph:
  *    1. LVGL hands us a Unicode codepoint (it has already decoded the UTF-8
  *       label text for us).
  *    2. unicode -> GBK via 1:/SYSTEM/FONT/UNIGBK.BIN.  The file is a flat
  *       array of 4-byte records sorted ascending by Unicode:
  *           [unicode_lo, unicode_hi, gbk_lo, gbk_hi]   (little-endian)
  *       so a binary search maps the codepoint to its 2-byte GBK code.
  *    3. GBK code -> raw bitmap from the matching GBKxx.FON via
  *       lcd_driver_get_hzmat_raw().  The stored bitmap is MSB-first, column
  *       scan (one column = ceil(height/8) bytes).
  *    4. Transpose column-major MSB -> row-major MSB into a per-instance scratch
  *       buffer.  LVGL 1bpp bitmaps are row-major, MSB-first
  *       (bit_ofs = row*width_bytes + col), so this single transpose is the
  *       only reformatting needed.
  *
  *  The callbacks are synchronous and LVGL draws one glyph at a time, so a
  *  single static scratch buffer per font instance is safe.
  ******************************************************************************
  */
#include "lv_port_font.h"
#include "drv_oled_text.h"
#include "ascii_1608_table.h"
#include "ascii_2412_table.h"
#include <string.h>

/* 32x32 dot == 128 bytes, the largest glyph we support. */
#define MAX_GLYPH_BYTES 128

typedef struct
{
    const pFONT *pf;                   /* selects .FON file + pixel size      */
    uint8_t      buf[MAX_GLYPH_BYTES]; /* row-major MSB bitmap for LVGL       */
} gbk_font_ctx_t;

static gbk_font_ctx_t gbk_ctx_12;
static gbk_font_ctx_t gbk_ctx_16;
static gbk_font_ctx_t gbk_ctx_24;
static gbk_font_ctx_t gbk_ctx_32;

/**
  * @brief  MSB-first column scan -> MSB-first row scan transpose.
  */
static void glyph_transpose(uint16_t w, uint16_t h,
                            const uint8_t *src, uint8_t *dst)
{
    uint16_t col_bytes = (uint16_t)((h + 7) / 8);
    uint16_t row_bytes = (uint16_t)((w + 7) / 8);

    memset(dst, 0, (size_t)row_bytes * h);

    for (uint16_t r = 0; r < h; r++)
    {
        for (uint16_t c = 0; c < w; c++)
        {
            /* bit for (column c, row r) in the source, MSB-first */
            if (src[(size_t)c * col_bytes + (r >> 3)] & (0x80u >> (r & 7)))
            {
                dst[(size_t)r * row_bytes + (c >> 3)] |= (uint8_t)(0x80u >> (c & 7));
            }
        }
    }
}

static bool gbk_get_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc,
                        uint32_t letter, uint32_t letter_next)
{
    (void)letter_next;

    gbk_font_ctx_t *ctx = (gbk_font_ctx_t *)font->dsc;
    uint8_t gbk[2];

    /* ASCII / Latin-1 are better served by the Montserrat fallback; only
     * attempt the SD lookup for non-ASCII codepoints. */
    if (letter < 0x80U)
    {
        return false;
    }
    if (lcd_driver_unigbk_lookup(letter, gbk) != RT_OK)
    {
        return false;   /* not present in GBK -> let the fallback try */
    }

    dsc->resolved_font  = font;
    dsc->adv_w          = ctx->pf->Width;
    dsc->box_w          = ctx->pf->Width;
    dsc->box_h          = ctx->pf->Height;
    dsc->ofs_x          = 0;
    dsc->ofs_y          = 0;
    dsc->bpp            = 1;
    dsc->is_placeholder = 0;
    return true;
}

static const uint8_t *gbk_get_bitmap(const lv_font_t *font, uint32_t letter)
{
    gbk_font_ctx_t *ctx = (gbk_font_ctx_t *)font->dsc;
    uint8_t gbk[2];
    static uint8_t raw[MAX_GLYPH_BYTES];

    if (lcd_driver_unigbk_lookup(letter, gbk) != RT_OK)
    {
        return NULL;
    }
    if (lcd_driver_get_hzmat_raw(gbk, raw, ctx->pf) != RT_OK)
    {
        return NULL;
    }
    glyph_transpose(ctx->pf->Width, ctx->pf->Height, raw, ctx->buf);
    return ctx->buf;
}

/* ===========================================================================
 * ASCII fallback: 8x16 dot-matrix table ported from the bare-metal project.
 * Replaces the anti-aliased Montserrat fallback (which looked blurry when
 * mixed with the crisp dot-matrix Chinese glyphs).
 * ===========================================================================*/

static bool ascii_get_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc,
                          uint32_t letter, uint32_t letter_next)
{
    (void)font;
    (void)letter_next;

    /* Only 0x20-0x7E are present in the table. */
    if (letter < 0x20U || letter > 0x7EU)
    {
        return false;
    }

    dsc->resolved_font  = font;
    dsc->adv_w          = 8;
    dsc->box_w          = 8;
    dsc->box_h          = 16;
    dsc->ofs_x          = 0;
    dsc->ofs_y          = 0;
    dsc->bpp            = 1;
    dsc->is_placeholder = 0;
    return true;
}

static const uint8_t *ascii_get_bitmap(const lv_font_t *font, uint32_t letter)
{
    (void)font;

    if (letter < 0x20U || letter > 0x7EU)
    {
        return NULL;
    }
    return &ascii_1608_msb[(uint32_t)(letter - 0x20U) * 16U];
}

lv_font_t gbk_ascii_font_16 = {
    .get_glyph_dsc    = ascii_get_dsc,
    .get_glyph_bitmap = ascii_get_bitmap,
    .line_height = 16, .base_line = 0, .subpx = LV_FONT_SUBPX_NONE,
    .dsc = NULL,
};

/* 12x24 ASCII fallback for the 24px GBK font (crisp at large sizes). */
static bool ascii24_get_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc,
                            uint32_t letter, uint32_t letter_next)
{
    (void)font;
    (void)letter_next;

    if (letter < 0x20U || letter > 0x7EU)
    {
        return false;
    }

    dsc->resolved_font  = font;
    dsc->adv_w          = 12;
    dsc->box_w          = 12;
    dsc->box_h          = 24;
    dsc->ofs_x          = 0;
    dsc->ofs_y          = 0;
    dsc->bpp            = 1;
    dsc->is_placeholder = 0;
    return true;
}

static const uint8_t *ascii24_get_bitmap(const lv_font_t *font, uint32_t letter)
{
    (void)font;

    if (letter < 0x20U || letter > 0x7EU)
    {
        return NULL;
    }
    return &ascii_2412_msb[(uint32_t)(letter - 0x20U) * 48U];
}

lv_font_t gbk_ascii_font_24 = {
    .get_glyph_dsc    = ascii24_get_dsc,
    .get_glyph_bitmap = ascii24_get_bitmap,
    .line_height = 24, .base_line = 0, .subpx = LV_FONT_SUBPX_NONE,
    .dsc = NULL,
};

/* One lv_font_t per dot size. base_line = 0 (top aligned) is what dot-matrix
 * fonts expect. line_height equals the glyph height. */
lv_font_t gbk_font_12 = {
    .get_glyph_dsc    = gbk_get_dsc,
    .get_glyph_bitmap = gbk_get_bitmap,
    .line_height = 12, .base_line = 0, .subpx = LV_FONT_SUBPX_NONE,
    .dsc = &gbk_ctx_12,
};
lv_font_t gbk_font_16 = {
    .get_glyph_dsc    = gbk_get_dsc,
    .get_glyph_bitmap = gbk_get_bitmap,
    .line_height = 16, .base_line = 0, .subpx = LV_FONT_SUBPX_NONE,
    .dsc = &gbk_ctx_16,
};
lv_font_t gbk_font_24 = {
    .get_glyph_dsc    = gbk_get_dsc,
    .get_glyph_bitmap = gbk_get_bitmap,
    .line_height = 24, .base_line = 0, .subpx = LV_FONT_SUBPX_NONE,
    .dsc = &gbk_ctx_24,
};
lv_font_t gbk_font_32 = {
    .get_glyph_dsc    = gbk_get_dsc,
    .get_glyph_bitmap = gbk_get_bitmap,
    .line_height = 32, .base_line = 0, .subpx = LV_FONT_SUBPX_NONE,
    .dsc = &gbk_ctx_32,
};

void lv_port_font_init(void)
{
    gbk_ctx_12.pf = &CH_TEXT_Font12;
    gbk_ctx_16.pf = &CH_TEXT_Font16;
    gbk_ctx_24.pf = &CH_TEXT_Font24;
    gbk_ctx_32.pf = &CH_TEXT_Font32;

    /* ASCII fallback: 8x16 dot-matrix for 12/16/32px fonts,
     *                12x24 dot-matrix for the 24px font. */
    gbk_font_12.fallback = &gbk_ascii_font_16;
    gbk_font_16.fallback = &gbk_ascii_font_16;
    gbk_font_24.fallback = &gbk_ascii_font_24;
    gbk_font_32.fallback = &gbk_ascii_font_16;
}
