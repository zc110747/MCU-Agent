/**
  ******************************************************************************
  * @file    ctf_format.h
  * @brief   CTF v1 (Character Table Font) on-disk format - MCU side.
  *
  *  This header mirrors tools/ttf2ctf/ctf_format.py field by field.  Both sides
  *  serialise explicitly, offset by offset, in little endian - the layout never
  *  depends on a C struct being packed a particular way.  Changing an offset or
  *  a width is a format change: bump CTF_VERSION and update both sides plus
  *  tools/ttf2ctf/README.md.
  *
  *  CTF is an *index* into an existing TTF.  It carries no bitmaps and no copy
  *  of the glyf data; the .ttf must stay next to the .ctf on the SD card.
  *
  *  File layout
  *  -----------
  *      CTF Header                  80 B
  *      TTF table index             N * 12 B
  *      Level-1 plane index         256 * 8 B
  *      Page index                  M * 40 B
  *      Entry table                 K * 24 B
  *
  *  Addressing a code point is three small reads, all cacheable:
  *
  *      plane = (u >> 16) & 0xFF  -> L1 record   (page_offset, page_count)
  *      page  = (u >>  8) & 0xFF  -> page record (entry_offset, 256-bit map)
  *      low   =  u        & 0xFF  -> bit test, then popcount rank -> entry
  *
  *  A cleared bit means "this font has no such character": the lookup returns
  *  CTF_NOT_FOUND right there and the TTF is never touched.  That property is
  *  the whole reason this file exists.
  ******************************************************************************
  */
#ifndef __CTF_FORMAT_H
#define __CTF_FORMAT_H

#include <stdint.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Identity and geometry                                                      */
/* -------------------------------------------------------------------------- */

/** 'CTF1' as a little-endian uint32. */
#define CTF_MAGIC              0x31465443u

/** The only version this firmware accepts. */
#define CTF_VERSION            1u

#define CTF_HEADER_SIZE        80u
#define CTF_TABLE_SIZE         12u
#define CTF_L1_SIZE            8u
#define CTF_PAGE_SIZE          40u
#define CTF_ENTRY_SIZE         24u

/** Level-1 slots: (u >> 16) & 0xFF, i.e. Unicode planes 0..255. */
#define CTF_L1_ENTRIES         256u
/** Pages per plane: (u >> 8) & 0xFF. */
#define CTF_PAGES_PER_PLANE    256u
/** Code points per page: u & 0xFF. */
#define CTF_PAGE_SPAN          256u
/** Presence bitmap bytes per page (256 bits). */
#define CTF_PAGE_BITMAP_BYTES  32u

/** Sparse Unicode, multi-level direct addressing. */
#define CTF_MODE_UNICODE       0u

/** Highest code point the addressing scheme can express. */
#define CTF_MAX_CODEPOINT      0xFFFFFFu

/* -------------------------------------------------------------------------- */
/* Header flags                                                               */
/* -------------------------------------------------------------------------- */

#define CTF_FLAG_HAS_COMPOSITE 0x00000001u
#define CTF_FLAG_HAS_KERN      0x00000002u
#define CTF_FLAG_HAS_GPOS      0x00000004u

/* -------------------------------------------------------------------------- */
/* Glyph flags                                                                */
/* -------------------------------------------------------------------------- */

#define CTF_GLYPH_EMPTY        0x0001u  /**< real glyph, no outline (space)   */
#define CTF_GLYPH_SIMPLE       0x0002u  /**< numberOfContours > 0             */
#define CTF_GLYPH_COMPOSITE    0x0004u  /**< numberOfContours < 0             */
#define CTF_GLYPH_VALID        0x0008u  /**< entry built from a real glyph    */
#define CTF_GLYPH_MISSING      0x0010u  /**< .notdef - report as NOT_FOUND    */

/* -------------------------------------------------------------------------- */
/* Header field offsets (the wire format)                                     */
/* -------------------------------------------------------------------------- */

#define CTF_HDR_MAGIC              0u   /* u32 */
#define CTF_HDR_VERSION            4u   /* u16 */
#define CTF_HDR_HEADER_SIZE        6u   /* u16 */
#define CTF_HDR_FLAGS              8u   /* u32 */
#define CTF_HDR_TTF_SIZE           12u  /* u32 */
#define CTF_HDR_TTF_CRC32          16u  /* u32 */
#define CTF_HDR_UNICODE_MODE       20u  /* u32 */
#define CTF_HDR_TABLE_OFF          24u  /* u32 */
#define CTF_HDR_TABLE_COUNT        28u  /* u32 */
#define CTF_HDR_L1_OFF             32u  /* u32 */
#define CTF_HDR_L1_COUNT           36u  /* u32 */
#define CTF_HDR_PAGE_OFF           40u  /* u32 */
#define CTF_HDR_PAGE_COUNT         44u  /* u32 */
#define CTF_HDR_ENTRY_OFF          48u  /* u32 */
#define CTF_HDR_ENTRY_COUNT        52u  /* u32 */
#define CTF_HDR_ENTRY_SIZE         56u  /* u32 */
#define CTF_HDR_UNITS_PER_EM       60u  /* u32 */
#define CTF_HDR_ASCENT             64u  /* i16 */
#define CTF_HDR_DESCENT            66u  /* i16 */
#define CTF_HDR_LINE_GAP           68u  /* i16 */
#define CTF_HDR_NUM_GLYPHS         70u  /* u16 */
#define CTF_HDR_CHAR_COUNT         72u  /* u32 */
#define CTF_HDR_RESERVED0          76u  /* u32 */

/* -------------------------------------------------------------------------- */
/* Record field offsets                                                       */
/* -------------------------------------------------------------------------- */

#define CTF_TBL_TAG                0u   /* u32 */
#define CTF_TBL_OFFSET             4u   /* u32 */
#define CTF_TBL_LENGTH             8u   /* u32 */

#define CTF_L1_PAGE_OFFSET         0u   /* u32, 0 = plane is empty */
#define CTF_L1_PAGE_COUNT          4u   /* u16 */
#define CTF_L1_RESERVED            6u   /* u16 */

#define CTF_PAGE_ENTRY_OFFSET      0u   /* u32 */
#define CTF_PAGE_ENTRY_COUNT       4u   /* u16 */
#define CTF_PAGE_FLAGS             6u   /* u16 */
#define CTF_PAGE_BITMAP            8u   /* u8[32] */

#define CTF_ENT_GLYF_OFFSET        0u   /* u32 */
#define CTF_ENT_GLYF_LENGTH        4u   /* u32 */
#define CTF_ENT_GLYPH_ID           8u   /* u16 */
#define CTF_ENT_ADVANCE_WIDTH      10u  /* u16 */
#define CTF_ENT_BEARING_X          12u  /* i16 */
#define CTF_ENT_X_MIN              14u  /* i16 */
#define CTF_ENT_Y_MIN              16u  /* i16 */
#define CTF_ENT_X_MAX              18u  /* i16 */
#define CTF_ENT_Y_MAX              20u  /* i16 */
#define CTF_ENT_FLAGS              22u  /* u16 */

/* -------------------------------------------------------------------------- */
/* Decoded records                                                            */
/* -------------------------------------------------------------------------- */

/** Header, decoded into native fields. */
typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t flags;
    uint32_t ttf_size;          /**< size of the TTF this index belongs to   */
    uint32_t ttf_crc32;         /**< 0 when the generator ran with --no-crc  */
    uint32_t unicode_mode;
    uint32_t table_index_offset;
    uint32_t table_index_count;
    uint32_t l1_index_offset;
    uint32_t l1_index_count;
    uint32_t page_index_offset;
    uint32_t page_index_count;
    uint32_t entry_offset;
    uint32_t entry_count;
    uint32_t entry_size;
    uint32_t units_per_em;
    int16_t  ascent;            /**< hhea.ascender, font units               */
    int16_t  descent;           /**< hhea.descender                          */
    int16_t  line_gap;          /**< hhea.lineGap                            */
    uint16_t num_glyphs;
    uint32_t char_count;
} ctf_header_t;

/** TTF table directory entry: lets the MCU skip parsing the sfnt directory. */
typedef struct
{
    uint32_t tag;
    uint32_t offset;
    uint32_t length;
} ctf_table_t;

/** One Unicode plane.  page_offset is a *file* offset; 0 means "empty". */
typedef struct
{
    uint32_t page_offset;
    uint16_t page_count;
} ctf_l1_t;

/** One 256-code-point slice.  Bit i set == low byte i exists in this font. */
typedef struct
{
    uint32_t entry_offset;
    uint16_t entry_count;
    uint16_t flags;
    uint8_t  bitmap[CTF_PAGE_BITMAP_BYTES];
} ctf_page_t;

/** One character.  The code point is implied by the entry's position. */
typedef struct
{
    uint32_t glyf_offset;       /**< absolute byte offset inside the TTF     */
    uint32_t glyf_length;
    uint16_t glyph_id;
    uint16_t advance_width;     /**< font units                              */
    int16_t  bearing_x;
    int16_t  x_min;
    int16_t  y_min;
    int16_t  x_max;
    int16_t  y_max;
    uint16_t flags;
} ctf_entry_t;

/* -------------------------------------------------------------------------- */
/* Little-endian loaders                                                      */
/* -------------------------------------------------------------------------- */

static inline uint16_t ctf_ld_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t ctf_ld_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline int16_t ctf_ld_i16(const uint8_t *p)
{
    return (int16_t)ctf_ld_u16(p);
}

/* -------------------------------------------------------------------------- */
/* Parsers                                                                    */
/* -------------------------------------------------------------------------- */

/**
  * @brief  Decode a 80-byte header blob.
  * @retval  0 on success, -1 when the buffer is not a sane CTF header.
  */
int ctf_header_parse(const uint8_t *b, ctf_header_t *h);

void ctf_table_parse(const uint8_t *b, ctf_table_t *t);
void ctf_l1_parse(const uint8_t *b, ctf_l1_t *r);
void ctf_page_parse(const uint8_t *b, ctf_page_t *p);
void ctf_entry_parse(const uint8_t *b, ctf_entry_t *e);

/* -------------------------------------------------------------------------- */
/* Page bitmap helpers                                                        */
/* -------------------------------------------------------------------------- */

/** SWAR popcount - Cortex-M7 has no POPCNT instruction. */
static inline uint32_t ctf_popcount8(uint8_t v)
{
    v = (uint8_t)((v & 0x55u) + ((v >> 1) & 0x55u));
    v = (uint8_t)((v & 0x33u) + ((v >> 2) & 0x33u));
    return (uint32_t)((v & 0x0Fu) + ((v >> 4) & 0x0Fu));
}

/** Is low byte @p low present in this page? */
static inline int ctf_page_has(const ctf_page_t *p, uint32_t low)
{
    return (int)((p->bitmap[low >> 3] >> (low & 7u)) & 1u);
}

/** Index of @p low within the page's entry run (count of set bits below it). */
static inline uint32_t ctf_page_rank(const ctf_page_t *p, uint32_t low)
{
    uint32_t n = 0u;
    uint32_t i;
    uint32_t bits;

    for (i = 0u; i < (low >> 3); i++)
    {
        n += ctf_popcount8(p->bitmap[i]);
    }
    bits = low & 7u;
    if (bits != 0u)
    {
        n += ctf_popcount8((uint8_t)(p->bitmap[low >> 3] & ((1u << bits) - 1u)));
    }
    return n;
}

/** An entry with MISSING set, or without VALID, counts as absent. */
static inline int ctf_entry_is_missing(const ctf_entry_t *e)
{
    return ((e->flags & CTF_GLYPH_MISSING) != 0u) ||
           ((e->flags & CTF_GLYPH_VALID) == 0u);
}

/** Empty outline (space) but a real, advance-bearing glyph. */
static inline int ctf_entry_is_empty(const ctf_entry_t *e)
{
    return (e->flags & CTF_GLYPH_EMPTY) != 0u;
}

/* -------------------------------------------------------------------------- */
/* Scaling - must match stb_truetype bit for bit                              */
/* -------------------------------------------------------------------------- */

/**
  * @brief  Scale a font-unit advance the way stb does it.
  *
  *  stb_truetype's own LVGL front end uses floor(adv * scale + 0.5).  Using
  *  anything else (roundf, lrintf, integer rounding with a different tie rule)
  *  shifts every glyph by up to a pixel against what the rasteriser draws.
  */
static inline int32_t ctf_scale_advance(int32_t v, float scale)
{
    return (int32_t)floorf(((float)v * scale) + 0.5f);
}

static inline int32_t ctf_floor_f(float v)
{
    return (int32_t)floorf(v);
}

static inline int32_t ctf_ceil_f(float v)
{
    return (int32_t)ceilf(v);
}

/**
  * @brief  Bitmap box of an entry at @p scale, in pixels.
  *
  *  This mirrors stbtt_GetGlyphBitmapBoxSubpixel() exactly:
  *
  *      ix0 = floor( x_min * scale)
  *      iy0 = floor(-y_max * scale)
  *      ix1 = ceil ( x_max * scale)
  *      iy1 = ceil (-y_min * scale)
  *
  *  The CTF stores the same xMin/yMin/xMax/yMax that stb reads out of the glyf
  *  header, so computing the descriptor here means LVGL can measure a glyph
  *  without touching the TTF at all.
  *
  *  LVGL then wants  box_w = ix1-ix0+1, box_h = iy1-iy0+1,
  *                   ofs_x = ix0,       ofs_y = -iy1.
  */
static inline void ctf_box_from_entry(const ctf_entry_t *e,
                                      float              scale,
                                      int32_t           *ix0,
                                      int32_t           *iy0,
                                      int32_t           *ix1,
                                      int32_t           *iy1)
{
    *ix0 = ctf_floor_f((float)e->x_min * scale);
    *iy0 = ctf_floor_f(-(float)e->y_max * scale);
    *ix1 = ctf_ceil_f((float)e->x_max * scale);
    *iy1 = ctf_ceil_f(-(float)e->y_min * scale);
}

#ifdef __cplusplus
}
#endif

#endif /* __CTF_FORMAT_H */
