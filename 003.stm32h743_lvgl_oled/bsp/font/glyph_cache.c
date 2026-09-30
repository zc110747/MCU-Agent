/**
  ******************************************************************************
  * @file    glyph_cache.c
  * @brief   Rasterised-glyph cache: 200 KB pool, LRU + page-epoch pinning.
  * @see     glyph_cache.h
  ******************************************************************************
  */
#include "glyph_cache.h"
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Storage                                                                    */
/*---------------------------------------------------------------------------*/

/* The pixel data lives in RAM_D2 (288 KB, free in this design).  Keeping the
 * 200 KB pool out of AXI-SRAM leaves the 512 KB D1 region for the rest of the
 * firmware and the LVGL frame buffer, and the glyph buffer is CPU-only (LVGL
 * reads it while drawing, the SPI DMA never touches it) so there is no cache
 * coherency concern. */
static uint8_t s_pool[GLYPH_CACHE_CAPACITY]
    __attribute__((section(".ram_d2"), aligned(4)));

typedef struct
{
    uint32_t off;
    uint32_t size;
} free_blk_t;

/* Entry table.  Compact array; a slot is free when used == 0. */
typedef struct
{
    uint32_t  unicode;
    uint16_t  px;
    uint16_t  w;
    uint16_t  h;
    uint32_t  off;     /* byte offset into s_pool                         */
    uint32_t  bytes;   /* allocated bytes (4-aligned)                     */
    uint32_t  lru;     /* access stamp; higher = more recent              */
    uint32_t  epoch;   /* page generation it belongs to                   */
    uint8_t   used;
} gc_entry_t;

static gc_entry_t s_ent[GLYPH_CACHE_MAX_ENTRIES];
static free_blk_t s_free[GLYPH_CACHE_MAX_ENTRIES + 1u];
static uint32_t   s_free_n;

static uint32_t   s_free_bytes;   /* total free bytes in s_pool            */
static uint32_t   s_lru;          /* monotonically increasing stamp        */
static uint32_t   s_epoch;        /* current page generation               */

/* Counters (never printed on a miss path). */
static uint32_t   s_hits;
static uint32_t   s_misses;
static uint32_t   s_evicts;

/*---------------------------------------------------------------------------*/
/* Heap (free-list with simple coalescing)                                    */
/*---------------------------------------------------------------------------*/

static void heap_init(void)
{
    s_free[0].off  = 0u;
    s_free[0].size = GLYPH_CACHE_CAPACITY;
    s_free_n       = 1u;
    s_free_bytes   = GLYPH_CACHE_CAPACITY;
}

/* Insert a free block keeping the list sorted by offset so neighbours are
 * adjacent and coalescing is a single linear scan. */
static void heap_free_insert(uint32_t off, uint32_t size)
{
    uint32_t i;
    uint32_t pos = s_free_n;

    for (i = 0u; i < s_free_n; i++)
    {
        if (s_free[i].off > off)
        {
            pos = i;
            break;
        }
    }

    /* Shift the tail up by one. */
    for (i = s_free_n; i > pos; i--)
    {
        s_free[i] = s_free[i - 1u];
    }

    s_free[pos].off  = off;
    s_free[pos].size = size;
    s_free_n++;

    /* Coalesce with the previous block. */
    if ((pos > 0u) && (s_free[pos - 1u].off + s_free[pos - 1u].size == off))
    {
        s_free[pos - 1u].size += size;
        /* Remove pos. */
        for (i = pos; i < (s_free_n - 1u); i++)
        {
            s_free[i] = s_free[i + 1u];
        }
        s_free_n--;
        pos--;
    }

    /* Coalesce with the next block. */
    if ((pos < (s_free_n - 1u)) &&
        (s_free[pos].off + s_free[pos].size == s_free[pos + 1u].off))
    {
        s_free[pos].size += s_free[pos + 1u].size;
        for (i = pos + 1u; i < (s_free_n - 1u); i++)
        {
            s_free[i] = s_free[i + 1u];
        }
        s_free_n--;
    }

    s_free_bytes += size;
}

/* First fit: returns 1 and the offset when a block >= need exists. */
static uint8_t heap_find(uint32_t need, uint32_t *out_off)
{
    uint32_t i;

    for (i = 0u; i < s_free_n; i++)
    {
        if (s_free[i].size >= need)
        {
            *out_off = s_free[i].off;
            return 1u;
        }
    }
    return 0u;
}

/* Allocate `need` (already 4-aligned) bytes; returns offset or ~0 on failure. */
static uint32_t heap_alloc(uint32_t need)
{
    uint32_t off;
    uint32_t i;

    if (heap_find(need, &off) == 0u)
    {
        return 0xFFFFFFFFu;
    }

    /* Locate the chosen block. */
    for (i = 0u; i < s_free_n; i++)
    {
        if (s_free[i].off == off)
        {
            break;
        }
    }

    if (i >= s_free_n)
    {
        return 0xFFFFFFFFu;
    }

    if (s_free[i].size > need)
    {
        s_free[i].off  += need;
        s_free[i].size -= need;
    }
    else
    {
        for (; i < (s_free_n - 1u); i++)
        {
            s_free[i] = s_free[i + 1u];
        }
        s_free_n--;
    }

    s_free_bytes -= need;
    return off;
}

static void heap_free(uint32_t off, uint32_t size)
{
    heap_free_insert(off, size);
}

/*---------------------------------------------------------------------------*/
/* Entry table helpers                                                        */
/*---------------------------------------------------------------------------*/

static gc_entry_t *entry_find(uint32_t unicode, uint16_t px)
{
    uint32_t i;

    for (i = 0u; i < GLYPH_CACHE_MAX_ENTRIES; i++)
    {
        gc_entry_t *e = &s_ent[i];

        if ((e->used != 0u) && (e->unicode == unicode) && (e->px == px))
        {
            return e;
        }
    }
    return NULL;
}

static gc_entry_t *entry_alloc(void)
{
    uint32_t i;

    for (i = 0u; i < GLYPH_CACHE_MAX_ENTRIES; i++)
    {
        if (s_ent[i].used == 0u)
        {
            return &s_ent[i];
        }
    }
    return NULL;
}

/*---------------------------------------------------------------------------*/
/* Public API                                                                 */
/*---------------------------------------------------------------------------*/

void glyph_cache_init(void)
{
    (void)memset(s_ent, 0, sizeof(s_ent));
    heap_init();
    s_lru        = 0u;
    s_epoch      = 0u;
    s_hits       = 0u;
    s_misses     = 0u;
    s_evicts     = 0u;
}

void glyph_cache_reset(void)
{
    (void)memset(s_ent, 0, sizeof(s_ent));
    heap_init();
    s_lru   = 0u;
    /* epoch is intentionally preserved across a reset so a warm reload of the
     * same page still pins correctly. */
}

void glyph_cache_reset_stats(void)
{
    s_hits   = 0u;
    s_misses = 0u;
    s_evicts = 0u;
}

void glyph_cache_bump_epoch(void)
{
    s_epoch++;
}

const uint8_t *glyph_cache_lookup(uint32_t unicode, uint16_t px,
                                 uint16_t *w, uint16_t *h, uint32_t *bytes)
{
    gc_entry_t *e = entry_find(unicode, px);

    if (e == NULL)
    {
        s_misses++;
        return NULL;
    }

    /* Promote to the current page so a glyph on screen right now can never be
     * reclaimed by LRU. */
    e->epoch = s_epoch;
    e->lru   = ++s_lru;

    *w     = e->w;
    *h     = e->h;
    *bytes = e->bytes;
    s_hits++;
    return &s_pool[e->off];
}

uint8_t *glyph_cache_insert(uint32_t unicode, uint16_t px,
                           uint16_t w, uint16_t h, uint32_t *bytes)
{
    gc_entry_t *e;
    uint32_t    need;
    uint32_t    off;
    uint32_t    i;

    /* Defensive: already present -> just hand the existing buffer back. */
    e = entry_find(unicode, px);
    if (e != NULL)
    {
        e->epoch = s_epoch;
        e->lru   = ++s_lru;
        *bytes   = e->bytes;
        return &s_pool[e->off];
    }

    need = (uint32_t)((uint32_t)w * (uint32_t)h);
    need = (need + 3u) & ~3u;          /* 4-byte align */
    if (need == 0u)
    {
        return NULL;
    }

    /* Evict LRU non-current-epoch entries until a fitting block exists. */
    while (heap_find(need, &off) == 0u)
    {
        int32_t  victim = -1;
        uint32_t worst  = 0u;
        uint8_t  first  = 1u;

        for (i = 0u; i < GLYPH_CACHE_MAX_ENTRIES; i++)
        {
            gc_entry_t *c = &s_ent[i];

            if ((c->used == 0u) || (c->epoch == s_epoch))
            {
                continue;               /* current page is pinned */
            }
            if (first || (c->lru < worst))
            {
                worst  = c->lru;
                victim = (int32_t)i;
                first  = 0u;
            }
        }

        if (victim < 0)
        {
            return NULL;                /* nothing evictable -> cannot fit */
        }

        heap_free(s_ent[victim].off, s_ent[victim].bytes);
        s_ent[victim].used = 0u;
        s_evicts++;
    }

    off = heap_alloc(need);
    if (off == 0xFFFFFFFFu)
    {
        return NULL;
    }

    e = entry_alloc();
    if (e == NULL)
    {
        heap_free(off, need);          /* table full: drop the reservation */
        return NULL;
    }

    e->unicode = unicode;
    e->px      = px;
    e->w       = w;
    e->h       = h;
    e->off     = off;
    e->bytes   = need;
    e->lru     = ++s_lru;
    e->epoch   = s_epoch;
    e->used    = 1u;

    *bytes = need;
    return &s_pool[off];
}

void glyph_cache_stats(uint32_t *hits, uint32_t *misses, uint32_t *evicts,
                      uint32_t *used_bytes, uint32_t *entries)
{
    uint32_t i;

    if (hits != NULL)    *hits    = s_hits;
    if (misses != NULL)  *misses  = s_misses;
    if (evicts != NULL)  *evicts  = s_evicts;

    if ((used_bytes != NULL) || (entries != NULL))
    {
        uint32_t used = 0u;
        uint32_t n    = 0u;

        for (i = 0u; i < GLYPH_CACHE_MAX_ENTRIES; i++)
        {
            if (s_ent[i].used != 0u)
            {
                used += s_ent[i].bytes;
                n++;
            }
        }
        if (used_bytes != NULL) *used_bytes = used;
        if (entries != NULL)    *entries    = n;
    }
}
