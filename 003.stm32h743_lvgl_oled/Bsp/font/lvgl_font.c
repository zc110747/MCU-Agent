/**
  ******************************************************************************
  * @file    lvgl_font.c
  * @brief   LVGL font backed by a CTF index plus the original TTF.
  * @see     lvgl_font.h
  *
  *  Two things make this fast enough for a 240x240 panel driven over SPI:
  *
  *   1. Measuring a glyph never touches the TTF.  The index carries advance and
  *      bounding box in font units, so get_glyph_dsc() is pure index arithmetic.
  *      LVGL measures every character on every layout and every draw, so this is
  *      the difference between a few dozen SD transactions per character and
  *      none.
  *
  *   2. Rendering a glyph happens once.  Rasterised bitmaps stay in a RAM pool
  *      until the pool wraps.
  *
  *  Kerning is deliberately not used.  LVGL 8.3 has no kerning concept at the
  *  label level - it advances by adv_w and nothing else - and stb's kern lookup
  *  walks GPOS one byte at a time, which is precisely the cost this redesign
  *  removes.
  ******************************************************************************
  */
#include "lvgl_font.h"
#include "log.h"

/* The Latin fallback is compiled-in Montserrat.  Without it, English and digits
 * would disappear whenever the SD card is missing. */
#if !LV_FONT_MONTSERRAT_12 || !LV_FONT_MONTSERRAT_16 || \
    !LV_FONT_MONTSERRAT_24 || !LV_FONT_MONTSERRAT_32
    #error "the CTF font engine needs LV_FONT_MONTSERRAT_12/16/24/32 as its Latin fallback"
#endif

#include "ctf_reader.h"
#include "ttf_reader.h"
#include "stb_adapter.h"
#include "glyph_cache.h"
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Configuration                                                              */
/*---------------------------------------------------------------------------*/

/* 16 KB x 4 is the block cache the design calls for.  stb reads TrueType
 * tables a field at a time, so this is what turns tens of thousands of SD
 * transactions into a handful. */
static uint8_t s_ttf_cache[TTF_BLOCK_SIZE * TTF_BLOCK_COUNT];

/* Index reads are tiny and clustered: 512 B x 8, plus a 2 KB copy of the
 * Level-1 table so the first hop of every lookup costs nothing. */
static uint8_t s_ctf_cache[CTF_BLOCK_SIZE * CTF_BLOCK_COUNT];
static uint8_t s_l1_shadow[CTF_L1_SHADOW_SIZE];

/**
  * Pool for the resident Level-2 page table.
  *
  * A page record is 40 B and covers 256 code points, so one Unicode plane needs
  * at most 256 * 40 = 10 KB.  Both HarmonyOS builds we ship report 256 pages
  * (BMP only), i.e. exactly 10240 B; the 288-page budget leaves room for a font
  * that also carries a handful of supplementary-plane pages without falling
  * back to reading page records off the card.
  *
  * Sized generously on purpose: this buffer is what makes a NOT_FOUND lookup
  * cost zero SD access, and 11 KB out of the ~200 KB still free in AXI-SRAM is
  * a good trade for that.
  */
#define CTF_PAGE_POOL_PAGES  288u
#define CTF_PAGE_POOL_SIZE   (CTF_PAGE_POOL_PAGES * CTF_PAGE_SIZE)

static uint8_t s_page_pool[CTF_PAGE_POOL_SIZE] __attribute__((aligned(4)));

/* Rasterised glyph bitmaps live in the LRU glyph cache (glyph_cache.c): a
 * 160 KB pool in RAM_D2, keyed by (unicode, px), with LRU eviction and
 * per-page pinning.  See glyph_cache.h. */

/*---------------------------------------------------------------------------*/
/* State                                                                      */
/*---------------------------------------------------------------------------*/

static const uint16_t s_sizes[CTF_FONT_SIZES] = { 12u, 16u, 24u, 32u };

typedef struct
{
    uint16_t px;
    float    scale;
} ctf_font_dsc_t;

static ttf_reader_t   s_ttf;
static ctf_reader_t   s_ctf;
static lv_font_t      s_font[CTF_FONT_SIZES];
static ctf_font_dsc_t s_fdsc[CTF_FONT_SIZES];

static uint8_t  s_ready;
static char     s_ctf_path[96];
static char     s_ttf_path[96];

/** What the index pinned in RAM; captured at init so the banner can print it. */
static ctf_resident_t s_resident;

/* Statistics.  All of them are just counters - never printed by a miss path. */
static uint32_t s_lookups;
static uint32_t s_missing;

/* ---- TTF glyph preload (async, page-aware) ------------------------------- */
#define PRELOAD_Q_MAX   256u
static uint32_t s_pl_uni[PRELOAD_Q_MAX];
static uint16_t s_pl_px[PRELOAD_Q_MAX];
static uint16_t s_pl_head;
static uint16_t s_pl_tail;
static uint16_t s_pl_count;
static lv_timer_t *s_pl_timer;

/*---------------------------------------------------------------------------*/
/* Rasterised glyph cache (LRU, 160 KB pool in RAM_D2)                        */
/*---------------------------------------------------------------------------*/

/** Drop every cached bitmap; the next draw re-rasterises it.  Thin wrapper so
  * the acceptance probe and engine init keep their bmp_flush() calls. */
static void bmp_flush(void)
{
    glyph_cache_reset();
}

/*---------------------------------------------------------------------------*/
/* TTF glyph preload (async, page-aware)                                      */
/*---------------------------------------------------------------------------*/

/** UTF-8 decoder: returns the next code point and advances *p; 0 at end. */
static uint32_t utf8_next(const char **p)
{
    const uint8_t *s = (const uint8_t *)*p;
    uint32_t       cp;
    uint8_t        c0;

    if (*s == '\0')
    {
        return 0u;
    }

    c0 = s[0];

    if (c0 < 0x80u)
    {
        cp = c0;
        *p = (const char *)(s + 1);
    }
    else if ((c0 & 0xE0u) == 0xC0u)
    {
        if (s[1] == '\0') { *p = (const char *)(s + 1); return 0u; }
        cp = ((uint32_t)(c0 & 0x1Fu) << 6) | (uint32_t)(s[1] & 0x3Fu);
        *p = (const char *)(s + 2);
    }
    else if ((c0 & 0xF0u) == 0xE0u)
    {
        if ((s[1] == '\0') || (s[2] == '\0')) { *p = (const char *)(s + 1); return 0u; }
        cp = ((uint32_t)(c0 & 0x0Fu) << 12) |
             ((uint32_t)(s[1] & 0x3Fu) << 6)  |
             (uint32_t)(s[2] & 0x3Fu);
        *p = (const char *)(s + 3);
    }
    else if ((c0 & 0xF8u) == 0xF0u)
    {
        if ((s[1] == '\0') || (s[2] == '\0') || (s[3] == '\0'))
        { *p = (const char *)(s + 1); return 0u; }
        cp = ((uint32_t)(c0 & 0x07u) << 18) |
             ((uint32_t)(s[1] & 0x3Fu) << 12) |
             ((uint32_t)(s[2] & 0x3Fu) << 6)  |
             (uint32_t)(s[3] & 0x3Fu);
        *p = (const char *)(s + 4);
    }
    else
    {
        *p = (const char *)(s + 1);   /* invalid lead byte: skip */
        return 0u;
    }

    return cp;
}

/** Scale (font_units -> px) for a requested pixel size. */
static float scale_for_px(uint16_t px)
{
    uint32_t i;

    for (i = 0u; i < CTF_FONT_SIZES; i++)
    {
        if (s_sizes[i] == px)
        {
            return s_fdsc[i].scale;
        }
    }
    for (i = CTF_FONT_SIZES; i > 0u; i--)
    {
        if (px >= s_sizes[i - 1u])
        {
            return s_fdsc[i - 1u].scale;
        }
    }
    return s_fdsc[0].scale;
}

static void preload_enqueue(uint32_t cp, uint16_t px)
{
    uint16_t i;
    uint16_t w = 0u, h = 0u;
    uint32_t bytes = 0u;

    /* NOTE: do NOT skip Latin here.  The HarmonyOS CTF index carries the Latin
     * range, so ctf_get_glyph_dsc() claims those code points and LVGL renders
     * them from the TTF (the Montserrat fallback only catches code points the
     * index genuinely lacks).  Skipping Latin made every page with Latin text
     * cold-rasterise it on first paint - a ~200 ms hitch on the font page.
     * Preloading Latin moves that cost to boot, when the SD card is already
     * being read for the CJK glyphs. */
    if (glyph_cache_lookup(cp, px, &w, &h, &bytes) != NULL)
    {
        return;     /* already cached */
    }
    for (i = 0u; i < s_pl_count; i++)
    {
        uint16_t idx = (uint16_t)((s_pl_head + i) % PRELOAD_Q_MAX);
        if ((s_pl_uni[idx] == cp) && (s_pl_px[idx] == px))
        {
            return; /* already queued */
        }
    }
    if (s_pl_count >= PRELOAD_Q_MAX)
    {
        return;     /* queue full: it will cold-rasterise on draw */
    }
    s_pl_uni[s_pl_tail] = cp;
    s_pl_px[s_pl_tail]  = px;
    s_pl_tail = (uint16_t)((s_pl_tail + 1u) % PRELOAD_Q_MAX);
    s_pl_count++;
}

static void preload_timer_cb(lv_timer_t *timer)
{
    uint16_t processed = 0u;

    LV_UNUSED(timer);

    /* A few glyphs per tick keeps the UI thread responsive; an SD read per
     * glyph is the cost, and most pages finish well within one second. */
    while ((s_pl_count > 0u) && (processed < 4u))
    {
        uint32_t    cp = s_pl_uni[s_pl_head];
        uint16_t    px = s_pl_px[s_pl_head];
        ctf_entry_t e;
        int32_t     ix0, iy0, ix1, iy1;
        uint16_t    w, h;
        uint32_t    bytes;
        uint8_t    *buf;

        s_pl_head = (uint16_t)((s_pl_head + 1u) % PRELOAD_Q_MAX);
        s_pl_count--;
        processed++;

        if (ctf_find_unicode(&s_ctf, cp, &e) != CTF_OK)
        {
            continue;   /* not in this font: Montserrat / nothing */
        }
        if (ctf_entry_is_empty(&e))
        {
            continue;   /* space-like: nothing to rasterise */
        }

        ctf_box_from_entry(&e, scale_for_px(px), &ix0, &iy0, &ix1, &iy1);
        w = (uint16_t)(ix1 - ix0 + 1);
        h = (uint16_t)(iy1 - iy0 + 1);
        if ((w == 0u) || (h == 0u))
        {
            continue;
        }

        buf = glyph_cache_insert(cp, px, w, h, &bytes);
        if (buf == NULL)
        {
            continue;   /* cache full beyond eviction: skip, redraw cold later */
        }
        (void)stb_adapter_render(e.glyph_id, px, buf, w, h,
                                 (int16_t)ix0, (int16_t)(-iy1));
    }

    if (s_pl_count == 0u)
    {
        if (s_pl_timer != NULL)
        {
            lv_timer_del(s_pl_timer);
            s_pl_timer = NULL;
        }
    }
}

uint16_t lvgl_font_px_of(const lv_font_t *f)
{
    uint32_t i;

    if (f == NULL)
    {
        return 0u;
    }
    for (i = 0u; i < CTF_FONT_SIZES; i++)
    {
        if (f == &s_font[i])
        {
            return s_sizes[i];
        }
    }
    return 0u;   /* not a CTF/TTF font (e.g. GBK) -> caller skips preload */
}

void lvgl_font_preload_text(const char *text, uint16_t px)
{
    const char *p = text;
    uint32_t    cp;

    if ((text == NULL) || (s_ready == 0u))
    {
        return;
    }

    while ((cp = utf8_next(&p)) != 0u)
    {
        preload_enqueue(cp, px);
    }

    if ((s_pl_count > 0u) && (s_pl_timer == NULL))
    {
        s_pl_timer = lv_timer_create(preload_timer_cb, 30, NULL);
    }
}

uint32_t lvgl_font_preload_pending(void)
{
    return (uint32_t)s_pl_count;
}

void lvgl_font_preload_label(const lv_obj_t *lbl)
{
    const lv_font_t *f;
    uint16_t         px;
    const char      *t;

    if ((s_ready == 0u) || (lbl == NULL))
    {
        return;
    }
    f = lv_obj_get_style_text_font(lbl, LV_PART_MAIN);
    px = lvgl_font_px_of(f);
    if (px == 0u)
    {
        return;     /* GBK / non-TTF font: no preload */
    }
    t = lv_label_get_text(lbl);
    if (t != NULL)
    {
        lvgl_font_preload_text(t, px);
    }
}

void lvgl_font_on_page_shown(void)
{
    /* The previously displayed page's glyphs fall behind the new epoch and are
     * reclaimed by LRU under pressure, while the new page's glyphs get promoted
     * to the current epoch on first draw (handled inside glyph_cache_lookup). */
    glyph_cache_bump_epoch();
}


/*---------------------------------------------------------------------------*/
/* LVGL callbacks                                                             */
/*---------------------------------------------------------------------------*/

static const lv_font_t *montserrat_for(uint16_t px)
{
    switch (px)
    {
        case 12u:  return &lv_font_montserrat_12;
        case 24u:  return &lv_font_montserrat_24;
        case 32u:  return &lv_font_montserrat_32;
        case 16u:
        default:   return &lv_font_montserrat_16;
    }
}

/**
  * Measure one glyph.  Pure index arithmetic - no TTF access, ever.
  *
  * @return false when the index does not carry this code point.  LVGL then
  *         tries font->fallback (built-in Montserrat) and, failing that, draws
  *         nothing.  This is a normal outcome and is never logged.
  */
static bool ctf_get_glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc,
                              uint32_t letter, uint32_t letter_next)
{
    const ctf_font_dsc_t *fd;
    ctf_entry_t           e;
    ctf_result_t          rc;
    int32_t               ix0, iy0, ix1, iy1;
    int32_t               adv;

    /* LVGL 8.3 advances by adv_w only - there is no kerning to apply, and
     * asking stb for one would mean scanning GPOS. */
    (void)letter_next;

    if ((font == NULL) || (dsc == NULL))
    {
        return false;
    }

    fd = (const ctf_font_dsc_t *)font->dsc;
    s_lookups++;

    rc = ctf_find_unicode(&s_ctf, letter, &e);

    if (rc != CTF_OK)
    {
        /* Not in this font.  Hand it to the fallback chain and stop here:
         * no TTF read, no log line, no placeholder bitmap. */
        s_missing++;
        return false;
    }

    adv = ctf_scale_advance((int32_t)e.advance_width, fd->scale);
    if (adv < 0)
    {
        adv = 0;
    }

    /* EMPTY and NOT_FOUND are different states.  A space has no outline but a
     * perfectly good advance, so it must lay out normally. */
    if (ctf_entry_is_empty(&e))
    {
        dsc->adv_w          = (uint16_t)adv;
        dsc->box_w          = 0u;
        dsc->box_h          = 0u;
        dsc->ofs_x          = 0;
        dsc->ofs_y          = 0;
        dsc->bpp            = 0u;
        dsc->is_placeholder = false;
        dsc->resolved_font  = NULL;
        return true;
    }

    ctf_box_from_entry(&e, fd->scale, &ix0, &iy0, &ix1, &iy1);

    dsc->adv_w          = (uint16_t)adv;
    dsc->box_w          = (uint16_t)(ix1 - ix0 + 1);
    dsc->box_h          = (uint16_t)(iy1 - iy0 + 1);
    dsc->ofs_x          = (int16_t)ix0;
    dsc->ofs_y          = (int16_t)(-iy1);
    dsc->bpp            = 8u;
    dsc->is_placeholder = false;
    dsc->resolved_font  = NULL;
    return true;
}

/**
  * Fetch the rasterised bitmap for a glyph, rendering it on a miss.
  *
  * @return NULL when the glyph is absent or empty, or when it is too large for
  *         the pool.  LVGL treats NULL as "draw nothing" or walks the fallback.
  */
static const uint8_t *ctf_get_glyph_bitmap(const lv_font_t *font, uint32_t letter)
{
    const ctf_font_dsc_t *fd;
    const uint8_t        *bmp;
    ctf_entry_t           e;
    ctf_result_t          rc;
    int32_t               ix0, iy0, ix1, iy1;
    uint16_t              w, h;
    uint32_t              bytes;
    uint8_t              *buf;

    if (font == NULL)
    {
        return NULL;
    }

    fd = (const ctf_font_dsc_t *)font->dsc;

    /* 1. Fast path: bitmap already in the LRU cache.  A hit also promotes the
     *    glyph to the current page epoch, so it is never reclaimed mid-screen. */
    bmp = glyph_cache_lookup(letter, fd->px, &w, &h, &bytes);
    if (bmp != NULL)
    {
        return bmp;
    }

    s_lookups++;

    /* 2. Cold miss: resolve the index entry, then rasterise into the cache. */
    rc = ctf_find_unicode(&s_ctf, letter, &e);
    if (rc != CTF_OK)
    {
        return NULL;
    }

    if (ctf_entry_is_empty(&e))
    {
        return NULL;    /* nothing to draw; the descriptor already advanced */
    }

    ctf_box_from_entry(&e, fd->scale, &ix0, &iy0, &ix1, &iy1);

    w = (uint16_t)(ix1 - ix0 + 1);
    h = (uint16_t)(iy1 - iy0 + 1);

    if ((w == 0u) || (h == 0u))
    {
        return NULL;
    }

    buf = glyph_cache_insert(letter, fd->px, w, h, &bytes);
    if (buf == NULL)
    {
        return NULL;    /* cache cannot make room - draw nothing rather than hang */
    }

    (void)stb_adapter_render(e.glyph_id, fd->px, buf, w, h,
                             (int16_t)ix0, (int16_t)(-iy1));

    /* The caller uses the bitmap immediately within this LVGL draw call, so it is
     * safe to return the pool pointer: eviction only frees other, non-current
     * entries, never the one we just returned. */
    return buf;
}

/*---------------------------------------------------------------------------*/
/* Public API                                                                 */
/*---------------------------------------------------------------------------*/

static void path_copy(char *dst, size_t dst_size, const char *src)
{
    size_t i;

    if (src == NULL)
    {
        dst[0] = '\0';
        return;
    }

    for (i = 0u; (src[i] != '\0') && (i < (dst_size - 1u)); i++)
    {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static void build_font(uint32_t i)
{
    const ctf_header_t *h = ctf_header(&s_ctf);
    float               scale;

    scale = (float)s_sizes[i] / (float)h->units_per_em;

    s_fdsc[i].px    = s_sizes[i];
    s_fdsc[i].scale = scale;

    s_font[i].dsc                = (const void *)&s_fdsc[i];
    s_font[i].get_glyph_dsc      = ctf_get_glyph_dsc;
    s_font[i].get_glyph_bitmap   = ctf_get_glyph_bitmap;
    s_font[i].line_height        = (lv_coord_t)(scale *
                                       ((float)h->ascent - (float)h->descent +
                                        (float)h->line_gap));
    s_font[i].base_line          = (lv_coord_t)(scale *
                                       ((float)h->line_gap - (float)h->descent));
    s_font[i].subpx              = 0u;
    s_font[i].underline_position = (int8_t)(-((int)(s_sizes[i] / 10u)) - 1);
    s_font[i].underline_thickness = 1;

    /* Latin comes from flash when the index has no such glyph. */
    s_font[i].fallback = montserrat_for(s_sizes[i]);
}

GlobalType_t lvgl_font_engine_init(const char *ctf_path, const char *ttf_path)
{
    uint32_t i;

    s_ready = 0u;
    (void)memset(&s_resident, 0, sizeof(s_resident));

    if ((ctf_path == NULL) || (ttf_path == NULL))
    {
        return RT_FAIL;
    }

    if (ctf_open(&s_ctf, ctf_path,
                 s_ctf_cache, CTF_BLOCK_SIZE, CTF_BLOCK_COUNT,
                 s_l1_shadow) != RT_OK)
    {
        return RT_FAIL;
    }

    /* Pin the front of the index now, while the card is quiet: the table
     * directory, and the whole page table if it fits.  A pool that is too small
     * is not fatal - the reader keeps serving page records from its cache - so
     * only a hard I/O error is worth aborting for. */
    if (ctf_load_resident(&s_ctf, s_page_pool, sizeof(s_page_pool),
                          &s_resident) != RT_OK)
    {
        ctf_close(&s_ctf);
        return RT_FAIL;
    }

    /* Refuse to render from a TTF that is not the one the index describes;
     * every offset in the index would point somewhere else. */
    if (ctf_verify_ttf(&s_ctf, ttf_path, 0) != RT_OK)
    {
        ctf_close(&s_ctf);
        return RT_FAIL;
    }

    if (ttf_open(&s_ttf, ttf_path,
                 s_ttf_cache, TTF_BLOCK_SIZE, TTF_BLOCK_COUNT) != RT_OK)
    {
        ctf_close(&s_ctf);
        return RT_FAIL;
    }

    if (stb_adapter_open(&s_ttf, 0u) != RT_OK)
    {
        ttf_close(&s_ttf);
        ctf_close(&s_ctf);
        return RT_FAIL;
    }

    (void)memset(s_font, 0, sizeof(s_font));
    (void)memset(s_fdsc, 0, sizeof(s_fdsc));

    for (i = 0u; i < CTF_FONT_SIZES; i++)
    {
        build_font(i);
    }

    glyph_cache_init();

    path_copy(s_ctf_path, sizeof(s_ctf_path), ctf_path);
    path_copy(s_ttf_path, sizeof(s_ttf_path), ttf_path);

    s_lookups     = 0u;
    s_missing     = 0u;
    glyph_cache_reset_stats();

    s_ready = 1u;
    return RT_OK;
}

void lvgl_font_engine_deinit(void)
{
    stb_adapter_close();
    ttf_close(&s_ttf);
    ctf_close(&s_ctf);
    bmp_flush();
    (void)memset(&s_resident, 0, sizeof(s_resident));
    s_ready = 0u;
}

int lvgl_font_engine_ready(void)
{
    return (int)s_ready;
}

const lv_font_t *lvgl_font_get(uint16_t px_size)
{
    uint32_t i;

    if (!s_ready)
    {
        return NULL;
    }

    /* Round down to the nearest size we instantiate. */
    for (i = CTF_FONT_SIZES; i > 0u; i--)
    {
        if (px_size >= s_sizes[i - 1u])
        {
            return &s_font[i - 1u];
        }
    }

    return &s_font[0];
}

const char *lvgl_font_ctf_path(void)
{
    return s_ctf_path;
}

const char *lvgl_font_ttf_path(void)
{
    return s_ttf_path;
}

/*---------------------------------------------------------------------------*/
/* On-target acceptance probe                                                */
/*---------------------------------------------------------------------------*/

/**
  *  A fixed vector, chosen to cover every branch of the lookup:
  *
  *    present CJK (dense), present CJK (worst case stroke count), present
  *    Latin, present digit, EMPTY (a space), a composite accent, plus code
  *    points no CJK font carries (astral plane, PUA) to exercise the
  *    NOT_FOUND path on real hardware.
  *
  *  Which of these are present is font dependent - the probe reports what it
  *  found rather than asserting it.
  */
static const struct
{
    uint32_t cp;
    uint16_t px;
} s_probe_vec[] = {
    { 0x4E2Du, 24u },   /* dense CJK                      */
    { 0x6587u, 24u },   /* dense CJK                      */
    { 0x91D1u, 32u },   /* 8 strokes at 32 px             */
    { 0x9F9Fu, 32u },   /* 30 strokes: worst-case arena   */
    { 0x0041u, 16u },   /* Latin capital                  */
    { 0x0030u, 16u },   /* digit                          */
    { 0x0020u, 16u },   /* EMPTY: advance, no outline     */
    { 0x00E9u, 32u },   /* composite accent               */
    { 0xFF0Cu, 24u },   /* fullwidth comma                */
    { 0x1F600u, 24u },  /* astral: absent from CJK fonts  */
    { 0xF8FFu, 24u },   /* PUA: absent from CJK fonts     */
    { 0x0378u, 24u },   /* unassigned Greek: absent       */
};

#define PROBE_VEC_N  (sizeof(s_probe_vec) / sizeof(s_probe_vec[0]))

/*---------------------------------------------------------------------------*/
/* Timing                                                                     */
/*---------------------------------------------------------------------------*/

/** HAL_GetTick() only resolves 1 ms, which is coarser than one glyph. */
static uint32_t cycles_to_us(uint32_t cycles)
{
    uint32_t mhz = (SystemCoreClock != 0u) ? (SystemCoreClock / 1000000u) : 1u;

    if (mhz == 0u)
    {
        mhz = 1u;
    }
    return cycles / mhz;
}

/** CYCCNT gives ns-class resolution; HAL_GetTick() only resolves 1 ms, which
 *  is coarser than a single glyph rasterisation. */
static void probe_timer_start(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

/** Cumulative microseconds the card has cost us so far (seek + read). */
static uint32_t sd_us_so_far(void)
{
    uint32_t sc, rc;

    ttf_cycles(&s_ttf, &sc, &rc);
    return cycles_to_us(sc + rc);
}

static uint32_t sd_reads_so_far(void)
{
    lvgl_font_stats_t st;

    lvgl_font_get_stats(&st);
    return st.ttf_fills;
}

uint32_t lvgl_font_selftest(lvgl_font_probe_t *out, uint32_t capacity)
{
    uint32_t i;
    uint32_t n = 0u;

    if ((out == NULL) || (capacity == 0u) || !s_ready)
    {
        return 0u;
    }

    if (capacity > PROBE_VEC_N)
    {
        capacity = (uint32_t)PROBE_VEC_N;
    }

    probe_timer_start();

    /* Everything below is timed cold, then warm. */
    bmp_flush();

    for (i = 0u; i < capacity; i++)
    {
        const lv_font_t      *f = lvgl_font_get(s_probe_vec[i].px);
        lv_font_glyph_dsc_t   dsc;
        lvgl_font_probe_t    *p = &out[n];
        const uint8_t        *bmp;
        uint32_t              reads0, reads1;
        uint32_t              sdus0, sdus1;
        uint32_t              c0, c1;
        uint32_t              j;

        (void)memset(p, 0, sizeof(*p));
        p->cp = s_probe_vec[i].cp;
        p->px = s_probe_vec[i].px;

        if (f == NULL)
        {
            n++;
            continue;
        }

        (void)memset(&dsc, 0, sizeof(dsc));
        reads0 = sd_reads_so_far();
        sdus0  = sd_us_so_far();

        /* Index arithmetic only - this must never touch the TTF.
         * lv_font_get_glyph_dsc() walks the fallback chain itself, so a hit is
         * only a hit when it resolves back to *this* font. */
        c0 = DWT->CYCCNT;
        {
            bool ok = lv_font_get_glyph_dsc(f, &dsc, p->cp, 0u);
            p->found = (ok && (dsc.resolved_font == f)) ? 1u : 0u;
        }
        c1 = DWT->CYCCNT;
        (void)c0; (void)c1;         /* tens of cycles - below timer interest */

        if (p->found != 0u)
        {
            p->adv_w = dsc.adv_w;
            p->box_w = dsc.box_w;
            p->box_h = dsc.box_h;
            p->ofs_x = dsc.ofs_x;
            p->ofs_y = dsc.ofs_y;
            p->empty = ((dsc.box_w == 0u) || (dsc.box_h == 0u) ||
                        (dsc.bpp == 0u)) ? 1u : 0u;
        }
        else
        {
            /* NOT_FOUND as far as the index is concerned.  Record what the
             * built-in font contributed so the log shows Latin still renders
             * when the index has nothing to offer. */
            p->fb_adv_w = dsc.adv_w;
        }

        /* Cold: rasterise + whatever SD traffic that costs. */
        c0 = DWT->CYCCNT;
        bmp = lv_font_get_glyph_bitmap(f, p->cp);
        c1 = DWT->CYCCNT;
        p->cold_us = cycles_to_us(c1 - c0);

        /* Warm: straight out of the bitmap pool. */
        c0 = DWT->CYCCNT;
        bmp = lv_font_get_glyph_bitmap(f, p->cp);
        c1 = DWT->CYCCNT;
        p->warm_us = cycles_to_us(c1 - c0);

        if ((bmp != NULL) && (p->box_w != 0u) && (p->box_h != 0u))
        {
            uint32_t bytes = (uint32_t)p->box_w * (uint32_t)p->box_h;
            uint32_t ink   = 0u;

            for (j = 0u; j < bytes; j++)
            {
                if (bmp[j] != 0u)
                {
                    ink++;
                }
            }
            p->ink = ink;
        }

        reads1 = sd_reads_so_far();
        sdus1  = sd_us_so_far();
        p->sd_reads = reads1 - reads0;
        p->sd_us    = sdus1 - sdus0;

        n++;
    }

    return n;
}

void lvgl_font_get_stats(lvgl_font_stats_t *out)
{
    uint32_t hits, misses, fills, fill_bytes;
    uint32_t c_look, c_nf, c_io;
    uint32_t p_ram, p_sd;
    uint32_t sc, rc;

    if (out == NULL)
    {
        return;
    }

    (void)memset(out, 0, sizeof(*out));

    out->lookups     = s_lookups;
    out->missing     = s_missing;

    {
        uint32_t gh, gm, ge, gb, gn;
        glyph_cache_stats(&gh, &gm, &ge, &gb, &gn);
        out->bmp_hits    = gh;
        out->bmp_misses  = gm;
        out->bmp_flushes = ge;   /* now: LRU evictions */
        out->bmp_bytes   = gb;
    }

    stb_adapter_arena_stats(&out->arena_peak, &out->arena_fails);

    ttf_stats(&s_ttf, &hits, &misses, &fills, &fill_bytes);
    out->ttf_hits   = hits;
    out->ttf_misses = misses;
    out->ttf_fills  = fills;
    out->ttf_bytes  = fill_bytes;

    ttf_cycles(&s_ttf, &sc, &rc);
    out->ttf_seek_us = cycles_to_us(sc);
    out->ttf_read_us = cycles_to_us(rc);

    ctf_stats(&s_ctf, &c_look, &c_nf, &c_io);
    out->ctf_lookups   = c_look;
    out->ctf_not_found = c_nf;
    out->ctf_io_errors = c_io;

    ctf_page_stats(&s_ctf, &p_ram, &p_sd);
    out->ctf_page_ram = p_ram;
    out->ctf_page_sd  = p_sd;
}

void lvgl_font_get_resident(ctf_resident_t *out)
{
    if (out == NULL)
    {
        return;
    }
    *out = s_resident;
}

uint32_t lvgl_font_resident_bytes(void)
{
    return s_resident.total_bytes;
}

uint32_t lvgl_font_page_pool_bytes(void)
{
    return (uint32_t)sizeof(s_page_pool);
}

void lvgl_font_reset_stats(void)
{
    s_lookups     = 0u;
    s_missing     = 0u;
    glyph_cache_reset_stats();
}

void lvgl_font_flush_bitmaps(void)
{
    bmp_flush();
}
