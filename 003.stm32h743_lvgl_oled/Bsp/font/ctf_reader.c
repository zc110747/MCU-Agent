/**
  ******************************************************************************
  * @file    ctf_reader.c
  * @brief   Implementation of the CTF index reader - see ctf_reader.h.
  ******************************************************************************
  */
#include "ctf_reader.h"
#include <string.h>

/* -------------------------------------------------------------------------- */
/* CRC32 (zlib polynomial, used to bind a CTF to its TTF)                     */
/*                                                                            */
/* The table is built on first use rather than stored in flash: it costs 1 KB  */
/* of RAM and a couple of thousand cycles, and only when a CRC is requested.   */
/* -------------------------------------------------------------------------- */

#define CRC_SCRATCH  1024u

static uint32_t s_crc_table[256];
static int      s_crc_ready = 0;

static void crc32_build(void)
{
    uint32_t i;
    uint32_t j;

    for (i = 0u; i < 256u; i++)
    {
        uint32_t c = i;
        for (j = 0u; j < 8u; j++)
        {
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        s_crc_table[i] = c;
    }
    s_crc_ready = 1;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, uint32_t len)
{
    uint32_t i;
    for (i = 0u; i < len; i++)
    {
        crc = s_crc_table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

/* -------------------------------------------------------------------------- */
/* Format decoders                                                            */
/* -------------------------------------------------------------------------- */

int ctf_header_parse(const uint8_t *b, ctf_header_t *h)
{
    if (b == NULL || h == NULL)
    {
        return -1;
    }

    h->magic              = ctf_ld_u32(b + CTF_HDR_MAGIC);
    h->version            = ctf_ld_u16(b + CTF_HDR_VERSION);
    h->header_size        = ctf_ld_u16(b + CTF_HDR_HEADER_SIZE);
    h->flags              = ctf_ld_u32(b + CTF_HDR_FLAGS);
    h->ttf_size           = ctf_ld_u32(b + CTF_HDR_TTF_SIZE);
    h->ttf_crc32          = ctf_ld_u32(b + CTF_HDR_TTF_CRC32);
    h->unicode_mode       = ctf_ld_u32(b + CTF_HDR_UNICODE_MODE);
    h->table_index_offset = ctf_ld_u32(b + CTF_HDR_TABLE_OFF);
    h->table_index_count  = ctf_ld_u32(b + CTF_HDR_TABLE_COUNT);
    h->l1_index_offset    = ctf_ld_u32(b + CTF_HDR_L1_OFF);
    h->l1_index_count     = ctf_ld_u32(b + CTF_HDR_L1_COUNT);
    h->page_index_offset  = ctf_ld_u32(b + CTF_HDR_PAGE_OFF);
    h->page_index_count   = ctf_ld_u32(b + CTF_HDR_PAGE_COUNT);
    h->entry_offset       = ctf_ld_u32(b + CTF_HDR_ENTRY_OFF);
    h->entry_count        = ctf_ld_u32(b + CTF_HDR_ENTRY_COUNT);
    h->entry_size         = ctf_ld_u32(b + CTF_HDR_ENTRY_SIZE);
    h->units_per_em       = ctf_ld_u32(b + CTF_HDR_UNITS_PER_EM);
    h->ascent             = ctf_ld_i16(b + CTF_HDR_ASCENT);
    h->descent            = ctf_ld_i16(b + CTF_HDR_DESCENT);
    h->line_gap           = ctf_ld_i16(b + CTF_HDR_LINE_GAP);
    h->num_glyphs         = ctf_ld_u16(b + CTF_HDR_NUM_GLYPHS);
    h->char_count         = ctf_ld_u32(b + CTF_HDR_CHAR_COUNT);

    if ((h->magic != CTF_MAGIC) ||
        (h->version != CTF_VERSION) ||
        (h->header_size != CTF_HEADER_SIZE) ||
        (h->entry_size != CTF_ENTRY_SIZE) ||
        (h->unicode_mode != CTF_MODE_UNICODE) ||
        (h->units_per_em == 0u) ||
        (h->l1_index_count == 0u) ||
        (h->l1_index_count > CTF_L1_ENTRIES) ||
        (h->entry_count == 0u) ||
        (h->table_index_count > 64u) ||
        (h->page_index_count > (CTF_L1_ENTRIES * CTF_PAGES_PER_PLANE)))
    {
        return -1;
    }
    return 0;
}

void ctf_table_parse(const uint8_t *b, ctf_table_t *t)
{
    t->tag    = ctf_ld_u32(b + CTF_TBL_TAG);
    t->offset = ctf_ld_u32(b + CTF_TBL_OFFSET);
    t->length = ctf_ld_u32(b + CTF_TBL_LENGTH);
}

void ctf_l1_parse(const uint8_t *b, ctf_l1_t *r)
{
    r->page_offset = ctf_ld_u32(b + CTF_L1_PAGE_OFFSET);
    r->page_count  = ctf_ld_u16(b + CTF_L1_PAGE_COUNT);
}

void ctf_page_parse(const uint8_t *b, ctf_page_t *p)
{
    p->entry_offset = ctf_ld_u32(b + CTF_PAGE_ENTRY_OFFSET);
    p->entry_count  = ctf_ld_u16(b + CTF_PAGE_ENTRY_COUNT);
    p->flags        = ctf_ld_u16(b + CTF_PAGE_FLAGS);
    (void)memcpy(p->bitmap, b + CTF_PAGE_BITMAP, CTF_PAGE_BITMAP_BYTES);
}

void ctf_entry_parse(const uint8_t *b, ctf_entry_t *e)
{
    e->glyf_offset   = ctf_ld_u32(b + CTF_ENT_GLYF_OFFSET);
    e->glyf_length   = ctf_ld_u32(b + CTF_ENT_GLYF_LENGTH);
    e->glyph_id      = ctf_ld_u16(b + CTF_ENT_GLYPH_ID);
    e->advance_width = ctf_ld_u16(b + CTF_ENT_ADVANCE_WIDTH);
    e->bearing_x     = ctf_ld_i16(b + CTF_ENT_BEARING_X);
    e->x_min         = ctf_ld_i16(b + CTF_ENT_X_MIN);
    e->y_min         = ctf_ld_i16(b + CTF_ENT_Y_MIN);
    e->x_max         = ctf_ld_i16(b + CTF_ENT_X_MAX);
    e->y_max         = ctf_ld_i16(b + CTF_ENT_Y_MAX);
    e->flags         = ctf_ld_u16(b + CTF_ENT_FLAGS);
}

/**
  * Does [off, off + count*rec) fit inside a file of @p total bytes?
  *
  * Written as an overflow-safe sequence: the multiplication is guarded first,
  * then the two comparisons never wrap.  A corrupt header must not be able to
  * produce a range that looks valid.
  */
static int range_ok(uint32_t off, uint32_t count, uint32_t rec, uint32_t total)
{
    uint32_t bytes;

    if (rec == 0u)
    {
        return 0;
    }
    if (count > (0xFFFFFFFFu / rec))
    {
        return 0;
    }
    bytes = count * rec;
    if (bytes > total)
    {
        return 0;
    }
    if (off > (total - bytes))
    {
        return 0;
    }
    return 1;
}

/* -------------------------------------------------------------------------- */
/* Backing store                                                              */
/* -------------------------------------------------------------------------- */

static GlobalType_t ctf_fill(void     *ctx,
                             uint32_t  offset,
                             uint8_t  *dst,
                             uint32_t  len,
                             uint32_t *got)
{
    ctf_reader_t *c = (ctf_reader_t *)ctx;
    UINT          br = 0u;

    if (f_lseek(&c->f, (FSIZE_t)offset) != FR_OK)
    {
        return RT_FAIL;
    }
    if (f_read(&c->f, dst, len, &br) != FR_OK)
    {
        return RT_FAIL;
    }
    *got = (uint32_t)br;
    return RT_OK;
}

/* -------------------------------------------------------------------------- */
/* Open / close                                                               */
/* -------------------------------------------------------------------------- */

GlobalType_t ctf_open(ctf_reader_t *c,
                      const char   *path,
                      uint8_t      *cache_buf,
                      uint32_t      block_size,
                      uint32_t      block_count,
                      uint8_t      *l1_shadow)
{
    uint8_t hdr[CTF_HEADER_SIZE];
    FRESULT res;

    if (c == NULL || path == NULL || cache_buf == NULL)
    {
        return RT_FAIL;
    }

    c->open       = 0u;
    c->size       = 0u;
    c->l1_shadow  = l1_shadow;
    c->l1_ready   = 0u;
    c->page_ready = 0u;
    c->page_plane = 0u;
    c->page_no    = 0u;
    c->lookups    = 0u;
    c->not_found  = 0u;
    c->io_errors  = 0u;
    (void)memset(&c->h, 0, sizeof(c->h));

    res = f_open(&c->f, path, FA_READ);
    if (res != FR_OK)
    {
        return RT_FAIL;
    }
    c->size = (uint32_t)f_size(&c->f);

    blkcache_init(&c->cache, cache_buf, block_size, block_count, c, ctf_fill);

    if (blkcache_read(&c->cache, 0u, hdr, CTF_HEADER_SIZE) != RT_OK)
    {
        (void)f_close(&c->f);
        return RT_FAIL;
    }

    if (ctf_header_parse(hdr, &c->h) != 0)
    {
        (void)f_close(&c->f);
        return RT_FAIL;
    }

    /* Every section must live inside the file, with no overflow anywhere. */
    if (!range_ok(c->h.table_index_offset, c->h.table_index_count, CTF_TABLE_SIZE, c->size) ||
        !range_ok(c->h.l1_index_offset,    c->h.l1_index_count,    CTF_L1_SIZE,    c->size) ||
        !range_ok(c->h.page_index_offset,  c->h.page_index_count,  CTF_PAGE_SIZE,  c->size) ||
        !range_ok(c->h.entry_offset,       c->h.entry_count,       CTF_ENTRY_SIZE, c->size))
    {
        (void)f_close(&c->f);
        return RT_FAIL;
    }

    if (l1_shadow != NULL)
    {
        if (blkcache_read(&c->cache, c->h.l1_index_offset,
                          l1_shadow, c->h.l1_index_count * CTF_L1_SIZE) == RT_OK)
        {
            c->l1_ready = 1u;
        }
    }

    c->open = 1u;
    return RT_OK;
}

void ctf_close(ctf_reader_t *c)
{
    if (c == NULL)
    {
        return;
    }
    if (c->open != 0u)
    {
        (void)f_close(&c->f);
    }
    blkcache_flush(&c->cache);
    c->open       = 0u;
    c->l1_ready   = 0u;
    c->page_ready = 0u;
}

int ctf_is_open(const ctf_reader_t *c)
{
    return (c != NULL && c->open != 0u) ? 1 : 0;
}

const ctf_header_t *ctf_header(const ctf_reader_t *c)
{
    return (c != NULL) ? &c->h : NULL;
}

/* -------------------------------------------------------------------------- */
/* Lookup                                                                     */
/* -------------------------------------------------------------------------- */

ctf_result_t ctf_find_unicode(ctf_reader_t *c, uint32_t cp, ctf_entry_t *out)
{
    uint32_t         plane;
    uint32_t         page_no;
    uint32_t         low;
    ctf_l1_t         l1;
    const ctf_page_t *pg;
    uint8_t          raw[CTF_PAGE_SIZE];
    uint32_t         entry_at;

    if (c == NULL || c->open == 0u)
    {
        return CTF_ERR_CLOSED;
    }
    if (out == NULL)
    {
        return CTF_ERR_IO;
    }

    c->lookups++;

    /* Step 0 - range.  The addressing scheme tops out at 0xFFFFFF. */
    if (cp > CTF_MAX_CODEPOINT)
    {
        return CTF_NOT_FOUND;
    }

    plane   = (cp >> 16) & 0xFFu;
    page_no = (cp >> 8) & 0xFFu;
    low     = cp & 0xFFu;

    /* Step 1 - Level 1: does this plane have any pages at all? */
    if (plane >= c->h.l1_index_count)
    {
        return CTF_NOT_FOUND;
    }

    if (c->l1_ready != 0u)
    {
        ctf_l1_parse(c->l1_shadow + (plane * CTF_L1_SIZE), &l1);
    }
    else
    {
        uint8_t raw_l1[CTF_L1_SIZE];
        if (blkcache_read(&c->cache,
                          c->h.l1_index_offset + (plane * CTF_L1_SIZE),
                          raw_l1, CTF_L1_SIZE) != RT_OK)
        {
            c->io_errors++;
            return CTF_ERR_IO;
        }
        ctf_l1_parse(raw_l1, &l1);
    }

    if (l1.page_offset == 0u || l1.page_count == 0u)
    {
        return CTF_NOT_FOUND;
    }
    if (page_no >= l1.page_count)
    {
        return CTF_NOT_FOUND;
    }

    /* Step 2 - page record, served from a one-entry cache. */
    if ((c->page_ready != 0u) &&
        (c->page_plane == plane) &&
        (c->page_no == page_no))
    {
        pg = &c->page;
    }
    else
    {
        if (blkcache_read(&c->cache,
                          l1.page_offset + (page_no * CTF_PAGE_SIZE),
                          raw, CTF_PAGE_SIZE) != RT_OK)
        {
            c->io_errors++;
            return CTF_ERR_IO;
        }
        ctf_page_parse(raw, &c->page);
        c->page_plane = plane;
        c->page_no    = page_no;
        c->page_ready = 1u;
        pg = &c->page;
    }

    /* Step 3 - presence bit.  This is the fast "no such character" exit. */
    if (ctf_page_has(pg, low) == 0)
    {
        c->not_found++;
        return CTF_NOT_FOUND;
    }

    /* Step 4 - the entry itself.  Two comparisons so "entry_at + size" can
     * never wrap back into a range that looks valid. */
    entry_at = pg->entry_offset + (ctf_page_rank(pg, low) * c->h.entry_size);
    if (entry_at > c->size)
    {
        return CTF_ERR_RANGE;
    }
    if (CTF_ENTRY_SIZE > (c->size - entry_at))
    {
        return CTF_ERR_RANGE;
    }
    if (blkcache_read(&c->cache, entry_at, raw, CTF_ENTRY_SIZE) != RT_OK)
    {
        c->io_errors++;
        return CTF_ERR_IO;
    }

    ctf_entry_parse(raw, out);

    /* Mapped to .notdef: the font has no glyph, so treat it as absent. */
    if (ctf_entry_is_missing(out) != 0)
    {
        c->not_found++;
        return CTF_NOT_FOUND;
    }

    return CTF_OK;
}

/* -------------------------------------------------------------------------- */
/* TTF binding                                                                */
/* -------------------------------------------------------------------------- */

GlobalType_t ctf_verify_ttf(const ctf_reader_t *c, const char *ttf_path, int do_crc)
{
    FIL      f;
    FRESULT  res;
    uint32_t size;

    if (c == NULL || c->open == 0u || ttf_path == NULL)
    {
        return RT_FAIL;
    }

    res = f_open(&f, ttf_path, FA_READ);
    if (res != FR_OK)
    {
        return RT_FAIL;
    }

    size = (uint32_t)f_size(&f);
    if (size != c->h.ttf_size)
    {
        (void)f_close(&f);
        return RT_FAIL;
    }

    if ((do_crc != 0) && (c->h.ttf_crc32 != 0u))
    {
        static uint8_t scratch[CRC_SCRATCH];
        uint32_t crc = 0xFFFFFFFFu;
        UINT     br;

        if (s_crc_ready == 0)
        {
            crc32_build();
        }
        for (;;)
        {
            if (f_read(&f, scratch, CRC_SCRATCH, &br) != FR_OK)
            {
                (void)f_close(&f);
                return RT_FAIL;
            }
            if (br == 0u)
            {
                break;
            }
            crc = crc32_update(crc, scratch, (uint32_t)br);
        }
        (void)f_close(&f);

        crc ^= 0xFFFFFFFFu;
        return (crc == c->h.ttf_crc32) ? RT_OK : RT_FAIL;
    }

    (void)f_close(&f);
    return RT_OK;
}

void ctf_stats(const ctf_reader_t *c,
               uint32_t *lookups, uint32_t *not_found, uint32_t *io_errors)
{
    if (c == NULL)
    {
        return;
    }
    if (lookups)    { *lookups    = c->lookups; }
    if (not_found)  { *not_found  = c->not_found; }
    if (io_errors)  { *io_errors  = c->io_errors; }
}
