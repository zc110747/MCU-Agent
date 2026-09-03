/**
  ******************************************************************************
  * @file    stb_adapter.c
  * @brief   stb_truetype wired to ttf_reader - see stb_adapter.h.
  *
  *  Nothing in stb_truetype is modified.  The only things supplied here are
  *    - the two stream macros that turn stb's per-byte reads into cached reads,
  *    - the scratch allocator,
  *    - a thin, allocation-free API around the three calls the LVGL backend
  *      actually needs.
  ******************************************************************************
  */
#include "stb_adapter.h"
#include <string.h>
#include <math.h>

/*---------------------------------------------------------------------------*/
/* Stream: one cursor over a cached TTF reader                                */
/*---------------------------------------------------------------------------*/

typedef struct
{
    ttf_reader_t *r;
    uint32_t      pos;
} stb_stream_t;

/**
  * A seek is a store.  stb seeks before every single read, so this is where
  * the design pays for itself - there is no f_lseek() to issue.
  */
static void stb_stream_seek(stb_stream_t *s, uint32_t pos)
{
    s->pos = pos;
}

/**
  * @note  On a short or failed read the destination is zero filled.  stb's
  *        ttBYTE()/ttUSHORT() helpers read into a local and return it without
  *        checking a result, so leaving the buffer alone would hand it stack
  *        garbage.  Zeros make it walk off the end of a table instead.
  */
static void stb_stream_read(stb_stream_t *s, void *dst, uint32_t len)
{
    uint8_t *d = (uint8_t *)dst;

    if (len != 0u)
    {
        if (ttf_read(s->r, s->pos, d, len) != RT_OK)
        {
            (void)memset(d, 0, (size_t)len);
        }
        s->pos += len;
    }
}

#define STBTT_STREAM_TYPE          stb_stream_t *
#define STBTT_STREAM_SEEK(s, x)    stb_stream_seek((s), (uint32_t)(x))
#define STBTT_STREAM_READ(s, x, y) stb_stream_read((s), (x), (uint32_t)(y))

/*---------------------------------------------------------------------------*/
/* Scratch allocator                                                          */
/*---------------------------------------------------------------------------*/

static uint8_t  s_arena[STB_ADAPTER_ARENA_SIZE] __attribute__((aligned(8)));
static uint32_t s_used;      /**< bump pointer                                */
static uint32_t s_live;      /**< allocations not yet freed                   */
static uint32_t s_peak;      /**< high-water mark                             */
static uint32_t s_fails;     /**< allocations the arena could not serve       */

/**
  * Bump allocation, bulk release.  stb allocates the vertex array, the edge
  * array and a chain of active-edge chunks for one glyph and frees all of them
  * before returning, so no per-block bookkeeping is needed: memory is handed
  * out of the arena and the whole arena is reclaimed the moment the last block
  * is freed.
  */
static void *stb_alloc(size_t n, void *u)
{
    uint32_t need;
    void    *p;

    (void)u;

    if (n == 0u)
    {
        return NULL;
    }

    need = (uint32_t)((n + 7u) & ~((size_t)7u));

    if (need > (STB_ADAPTER_ARENA_SIZE - s_used))
    {
        s_fails++;
        return NULL;
    }

    p = (void *)&s_arena[s_used];
    s_used += need;
    s_live++;

    if (s_used > s_peak)
    {
        s_peak = s_used;
    }

    return p;
}

static void stb_free(void *p, void *u)
{
    (void)p;
    (void)u;

    if (s_live != 0u)
    {
        s_live--;
    }

    if (s_live == 0u)
    {
        s_used = 0u;    /* everything is back - start the next glyph clean */
    }
}

/*---------------------------------------------------------------------------*/
/* stb_truetype                                                               */
/*---------------------------------------------------------------------------*/

#define STBTT_STATIC
#define STB_RECT_PACK_IMPLEMENTATION
#define STBRP_STATIC
#define STB_TRUETYPE_IMPLEMENTATION

/* stb's asserts are all "cannot happen" geometry checks.  A failed one must
 * not drag assert()/printf() into the rasteriser; reads are bounds checked and
 * zero filled, so carrying on is safe. */
#define STBTT_assert(x)                     ((void)0)
#define STBTT_malloc(x, u)                  stb_alloc((x), (u))
#define STBTT_free(x, u)                    stb_free((x), (u))
#define STBTT_HEAP_FACTOR_SIZE_32           64u
#define STBTT_HEAP_FACTOR_SIZE_128          32u
#define STBTT_HEAP_FACTOR_SIZE_DEFAULT      16u

#include "stb_rect_pack.h"
#include "stb_truetype_htcw.h"

static stbtt_fontinfo s_info;
static stb_stream_t   s_stream;
static uint8_t        s_ready;
static uint32_t       s_box_mismatch;   /**< CTF box disagreed with stb's box  */

/*---------------------------------------------------------------------------*/
/* Public API                                                                 */
/*---------------------------------------------------------------------------*/

GlobalType_t stb_adapter_open(ttf_reader_t *r, uint32_t font_index)
{
    int  offset;
    int  ok;

    s_ready = 0u;

    if (r == NULL)
    {
        return RT_FAIL;
    }

    s_stream.r   = r;
    s_stream.pos = 0u;

    /* stb copies the stream object into s_info, so it must outlive the call. */
    offset = stbtt_GetFontOffsetForIndex(&s_stream, (int)font_index);
    ok     = stbtt_InitFont(&s_info, &s_stream, offset);

    if (ok == 0)
    {
        return RT_FAIL;
    }

    s_used         = 0u;
    s_live         = 0u;
    s_box_mismatch = 0u;
    s_ready        = 1u;
    return RT_OK;
}

void stb_adapter_close(void)
{
    s_ready = 0u;
    s_used  = 0u;
    s_live  = 0u;
}

int stb_adapter_ready(void)
{
    return (int)s_ready;
}

GlobalType_t stb_adapter_metrics(int16_t *ascent,
                                 int16_t *descent,
                                 int16_t *line_gap)
{
    int a, d, g;

    if (!s_ready)
    {
        return RT_FAIL;
    }

    stbtt_GetFontVMetrics(&s_info, &a, &d, &g);

    if (ascent != NULL)
    {
        *ascent = (int16_t)a;
    }
    if (descent != NULL)
    {
        *descent = (int16_t)d;
    }
    if (line_gap != NULL)
    {
        *line_gap = (int16_t)g;
    }

    return RT_OK;
}

GlobalType_t stb_adapter_render(uint16_t glyph_id,
                                uint16_t px_size,
                                uint8_t *buf,
                                uint16_t box_w,
                                uint16_t box_h,
                                int16_t  ofs_x,
                                int16_t  ofs_y)
{
    float scale;
    int   ix0, iy0, ix1, iy1;

    if (!s_ready || (buf == NULL))
    {
        return RT_FAIL;
    }

    /* Recover from a previous call that ran out of arena: stb would have left
     * some blocks unfreed, so the bulk release never happened. */
    if (s_live != 0u)
    {
        s_used = 0u;
        s_live = 0u;
    }

    /* Space and friends: nothing to draw, but a real glyph with an advance. */
    if ((box_w == 0u) || (box_h == 0u))
    {
        return RT_OK;
    }

    scale = stbtt_ScaleForMappingEmToPixels(&s_info, (float)px_size);

    /* The CTF derived box is what LVGL positions the glyph by.  stb recomputes
     * the same numbers from the glyf header, so if the two ever disagree the
     * ink lands in the wrong place inside the box - worth counting, and it
     * costs one cached read of the glyph header. */
    stbtt_GetGlyphBitmapBox(&s_info, (int)glyph_id, scale, scale,
                            &ix0, &iy0, &ix1, &iy1);
    if ((ix0 != (int)ofs_x) || (iy1 != -(int)ofs_y))
    {
        s_box_mismatch++;
    }

    (void)memset(buf, 0, (size_t)box_w * (size_t)box_h);

    stbtt_MakeGlyphBitmap(&s_info, buf,
                          (int)box_w, (int)box_h, (int)box_w,
                          scale, scale, (int)glyph_id);

    return RT_OK;
}

int stb_adapter_kerning(uint16_t g1, uint16_t g2, uint16_t px_size)
{
    int   k;
    float scale;

    if (!s_ready)
    {
        return 0;
    }

    scale = stbtt_ScaleForMappingEmToPixels(&s_info, (float)px_size);
    k     = stbtt_GetGlyphKernAdvance(&s_info, (int)g1, (int)g2);

    return (int)floorf(((float)k * scale) + 0.5f);
}

void stb_adapter_arena_stats(uint32_t *peak, uint32_t *fails)
{
    if (peak != NULL)
    {
        *peak = s_peak;
    }
    if (fails != NULL)
    {
        *fails = s_fails;
    }
}

/**
  * @brief  How often the CTF-derived box disagreed with stb's own.
  *
  *  Must stay at zero.  Anything else means get_glyph_dsc() and the rasteriser
  *  are working from different geometry and the ink lands in the wrong place.
  */
uint32_t stb_adapter_box_mismatches(void)
{
    return s_box_mismatch;
}
