/**
  ******************************************************************************
  * @file    stb_adapter.h
  * @brief   stb_truetype wired to ttf_reader.
  *
  *  stb_truetype itself is untouched.  It is compiled here in STBTT_STREAM mode
  *  where every individual byte it wants goes through two macros:
  *
  *      STBTT_STREAM_SEEK(s, pos)          -> move a cursor, no I/O
  *      STBTT_STREAM_READ(s, dst, len)     -> ttf_read(), block cached
  *
  *  That is the whole trick behind the redesign.  stb reads TrueType tables one
  *  field at a time; left to itself that is one f_lseek()+f_read() per byte, and
  *  a single GPOS scan turns into tens of thousands of SD transactions.  The
  *  block cache in ttf_reader collapses those back into a handful of 16 KB
  *  reads, and the cursor makes seeks free.
  *
  *  Scratch memory
  *  --------------
  *  Rasterising a glyph makes stb allocate three transient arrays (vertices,
  *  edges, active edges).  They are all freed before the call returns, so they
  *  come from a small bump arena that is reset once everything has been freed.
  *  Going through the LVGL heap instead would fragment a 96 KB pool with
  *  multi-kilobyte, short-lived blocks.
  *
  *  If a glyph needs more than the arena holds, the allocation simply fails and
  *  stb draws nothing - no crash, no hang, just one blank character.
  ******************************************************************************
  */
#ifndef __STB_ADAPTER_H
#define __STB_ADAPTER_H

#include <stdint.h>
#include "main.h"
#include "ttf_reader.h"

/**
  * Scratch bytes for one rasterisation.  A busy CJK glyph costs roughly
  *   vertices  ~10 B/point
  *   edges     ~20 B/point
  *   actives   ~28 B/point
  * so ~60 B per outline point; 36 KB covers a ~600 point glyph.
  */
#ifndef STB_ADAPTER_ARENA_SIZE
#define STB_ADAPTER_ARENA_SIZE   (36u * 1024u)
#endif

/**
  * @brief  Parse the sfnt directory of an open TTF.
  * @param  r           opened reader - it must stay open while the adapter is used
  * @param  font_index  index inside a .ttc collection; 0 for a plain .ttf
  * @retval RT_OK when stb accepted the font
  */
GlobalType_t stb_adapter_open(ttf_reader_t *r, uint32_t font_index);

/** Drop the parsed font.  The reader itself is left open. */
void stb_adapter_close(void);

/** 1 when a font has been parsed successfully. */
int stb_adapter_ready(void);

/**
  * @brief  Vertical metrics in font units, as stb reads them.
  *
  *  Units-per-em is deliberately not here: this fork of stb has no accessor for
  *  it, and the CTF header carries the same number, so read it there.
  */
GlobalType_t stb_adapter_metrics(int16_t *ascent,
                                 int16_t *descent,
                                 int16_t *line_gap);

/**
  * @brief  Rasterise one glyph into a caller supplied 8 bpp buffer.
  *
  *  The buffer is the glyph's bitmap box, so @p box_w / @p box_h / @p ofs_x /
  *  @p ofs_y must be the values stb would have produced for this glyph at this
  *  size - see ctf_box_from_entry() in ctf_format.h.  Rendering uses stb's own
  *  box internally, so a mismatch would shift the ink inside the box.
  *
  * @param  glyph_id  glyph index from the CTF entry
  * @param  px_size   requested em size in pixels
  * @param  buf       box_w * box_h bytes, stride == box_w
  * @retval RT_OK when the glyph was drawn (or is empty)
  */
GlobalType_t stb_adapter_render(uint16_t glyph_id,
                                uint16_t px_size,
                                uint8_t *buf,
                                uint16_t box_w,
                                uint16_t box_h,
                                int16_t  ofs_x,
                                int16_t  ofs_y);

/**
  * @brief  Kern advance between two glyphs, in pixels.
  *
  *  Provided for benchmarks and for anyone drawing text themselves.  The LVGL
  *  backend deliberately never calls it: LVGL 8.3 labels have no notion of
  *  kerning, and stb scans GPOS one byte at a time, which is exactly the cost
  *  this redesign exists to avoid.
  */
int stb_adapter_kerning(uint16_t g1, uint16_t g2, uint16_t px_size);

/** Highest the arena ever got, and how many allocations it had to refuse. */
void stb_adapter_arena_stats(uint32_t *peak, uint32_t *fails);

/**
  * @brief  How often the CTF-derived bitmap box disagreed with stb's own.
  *
  *  Must stay at zero - see ctf_box_from_entry() in ctf_format.h.
  */
uint32_t stb_adapter_box_mismatches(void);

#endif /* __STB_ADAPTER_H */
