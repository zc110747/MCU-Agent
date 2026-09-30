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
/* 字形位图池：RAM_D2，绘制路径 CPU 直读，SPI DMA 不触碰 */
static uint8_t g_pool[GLYPH_CACHE_CAPACITY]
    __attribute__((section(".ram_d2"), aligned(4)));

typedef struct
{
    uint32_t off;
    uint32_t size;
} free_blk_t;

/* Entry table.  Compact array; a slot is free when used == 0. */
typedef struct
{
    uint32_t unicode;
    uint16_t px;
    uint16_t w;
    uint16_t h;
    uint32_t off;   /* byte offset into g_pool                         */
    uint32_t bytes; /* allocated bytes (4-aligned)                     */
    uint32_t lru;   /* access stamp; higher = more recent              */
    uint32_t epoch; /* page generation it belongs to                   */
    uint8_t  used;
} gc_entry_t;

/* 字形缓存堆状态：条目表、空闲链表与 LRU/代次 */
typedef struct
{
    gc_entry_t ent[GLYPH_CACHE_MAX_ENTRIES];
    free_blk_t free_list[GLYPH_CACHE_MAX_ENTRIES + 1u];
    uint32_t   free_n;
    uint32_t   free_bytes; /* total free bytes in the pool   */
    uint32_t   lru;        /* monotonically increasing stamp */
    uint32_t   epoch;      /* current page generation        */
} gc_state_t;

static gc_state_t g_gc = {0};

/* 命中统计（miss 路径不打印） */
typedef struct
{
    uint32_t hits;
    uint32_t misses;
    uint32_t evicts;
} gc_stats_t;

static gc_stats_t g_gc_stats = {0};

/*---------------------------------------------------------------------------*/
/* Heap (free-list with simple coalescing)                                    */
/*---------------------------------------------------------------------------*/

static void heap_init(void)
{
    g_gc.free_list[0].off  = 0u;
    g_gc.free_list[0].size = GLYPH_CACHE_CAPACITY;
    g_gc.free_n       = 1u;
    g_gc.free_bytes   = GLYPH_CACHE_CAPACITY;
}

/* Insert a free block keeping the list sorted by offset so neighbours are
 * adjacent and coalescing is a single linear scan. */
static void heap_free_insert(uint32_t off, uint32_t size)
{
    uint32_t i;
    uint32_t pos = g_gc.free_n;

    for (i = 0u; i < g_gc.free_n; i++)
    {
        if (g_gc.free_list[i].off > off)
        {
            pos = i;
            break;
        }
    }

    /* Shift the tail up by one. */
    for (i = g_gc.free_n; i > pos; i--)
    {
        g_gc.free_list[i] = g_gc.free_list[i - 1u];
    }

    g_gc.free_list[pos].off  = off;
    g_gc.free_list[pos].size = size;
    g_gc.free_n++;

    /* Coalesce with the previous block. */
    if ((pos > 0u) && (g_gc.free_list[pos - 1u].off + g_gc.free_list[pos - 1u].size == off))
    {
        g_gc.free_list[pos - 1u].size += size;
        /* Remove pos. */
        for (i = pos; i < (g_gc.free_n - 1u); i++)
        {
            g_gc.free_list[i] = g_gc.free_list[i + 1u];
        }
        g_gc.free_n--;
        pos--;
    }

    /* Coalesce with the next block. */
    if ((pos < (g_gc.free_n - 1u)) &&
        (g_gc.free_list[pos].off + g_gc.free_list[pos].size == g_gc.free_list[pos + 1u].off))
    {
        g_gc.free_list[pos].size += g_gc.free_list[pos + 1u].size;
        for (i = pos + 1u; i < (g_gc.free_n - 1u); i++)
        {
            g_gc.free_list[i] = g_gc.free_list[i + 1u];
        }
        g_gc.free_n--;
    }

    g_gc.free_bytes += size;
}

/* First fit: returns 1 and the offset when a block >= need exists. */
static uint8_t heap_find(uint32_t need, uint32_t *out_off)
{
    uint32_t i;

    for (i = 0u; i < g_gc.free_n; i++)
    {
        if (g_gc.free_list[i].size >= need)
        {
            *out_off = g_gc.free_list[i].off;
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
    for (i = 0u; i < g_gc.free_n; i++)
    {
        if (g_gc.free_list[i].off == off)
        {
            break;
        }
    }

    if (i >= g_gc.free_n)
    {
        return 0xFFFFFFFFu;
    }

    if (g_gc.free_list[i].size > need)
    {
        g_gc.free_list[i].off += need;
        g_gc.free_list[i].size -= need;
    }
    else
    {
        for (; i < (g_gc.free_n - 1u); i++)
        {
            g_gc.free_list[i] = g_gc.free_list[i + 1u];
        }
        g_gc.free_n--;
    }

    g_gc.free_bytes -= need;
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
        gc_entry_t *e = &g_gc.ent[i];

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
        if (g_gc.ent[i].used == 0u)
        {
            return &g_gc.ent[i];
        }
    }
    return NULL;
}

/*---------------------------------------------------------------------------*/
/* Public API                                                                 */
/*---------------------------------------------------------------------------*/

void glyph_cache_init(void)
{
    (void)memset(g_gc.ent, 0, sizeof(g_gc.ent));
    heap_init();
    g_gc.lru    = 0u;
    g_gc.epoch  = 0u;
    g_gc_stats.hits   = 0u;
    g_gc_stats.misses = 0u;
    g_gc_stats.evicts = 0u;
}

void glyph_cache_reset(void)
{
    (void)memset(g_gc.ent, 0, sizeof(g_gc.ent));
    heap_init();
    g_gc.lru = 0u;
    /* epoch is intentionally preserved across a reset so a warm reload of the
     * same page still pins correctly. */
}

void glyph_cache_reset_stats(void)
{
    g_gc_stats.hits   = 0u;
    g_gc_stats.misses = 0u;
    g_gc_stats.evicts = 0u;
}

void glyph_cache_bump_epoch(void)
{
    g_gc.epoch++;
}

const uint8_t *glyph_cache_lookup(uint32_t unicode, uint16_t px,
                                  uint16_t *w, uint16_t *h, uint32_t *bytes)
{
    gc_entry_t *e = entry_find(unicode, px);

    if (e == NULL)
    {
        g_gc_stats.misses++;
        return NULL;
    }

    /* Promote to the current page so a glyph on screen right now can never be
     * reclaimed by LRU. */
    e->epoch = g_gc.epoch;
    e->lru   = ++g_gc.lru;

    *w     = e->w;
    *h     = e->h;
    *bytes = e->bytes;
    g_gc_stats.hits++;
    return &g_pool[e->off];
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
        e->epoch = g_gc.epoch;
        e->lru   = ++g_gc.lru;
        *bytes   = e->bytes;
        return &g_pool[e->off];
    }

    need = (uint32_t)((uint32_t)w * (uint32_t)h);
    need = (need + 3u) & ~3u; /* 4-byte align */
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
            gc_entry_t *c = &g_gc.ent[i];

            if ((c->used == 0u) || (c->epoch == g_gc.epoch))
            {
                continue; /* current page is pinned */
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
            return NULL; /* nothing evictable -> cannot fit */
        }

        heap_free(g_gc.ent[victim].off, g_gc.ent[victim].bytes);
        g_gc.ent[victim].used = 0u;
        g_gc_stats.evicts++;
    }

    off = heap_alloc(need);
    if (off == 0xFFFFFFFFu)
    {
        return NULL;
    }

    e = entry_alloc();
    if (e == NULL)
    {
        heap_free(off, need); /* table full: drop the reservation */
        return NULL;
    }

    e->unicode = unicode;
    e->px      = px;
    e->w       = w;
    e->h       = h;
    e->off     = off;
    e->bytes   = need;
    e->lru     = ++g_gc.lru;
    e->epoch   = g_gc.epoch;
    e->used    = 1u;

    *bytes = need;
    return &g_pool[off];
}

void glyph_cache_stats(uint32_t *hits, uint32_t *misses, uint32_t *evicts,
                       uint32_t *used_bytes, uint32_t *entries)
{
    uint32_t i;

    if (hits != NULL)
        *hits = g_gc_stats.hits;
    if (misses != NULL)
        *misses = g_gc_stats.misses;
    if (evicts != NULL)
        *evicts = g_gc_stats.evicts;

    if ((used_bytes != NULL) || (entries != NULL))
    {
        uint32_t used = 0u;
        uint32_t n    = 0u;

        for (i = 0u; i < GLYPH_CACHE_MAX_ENTRIES; i++)
        {
            if (g_gc.ent[i].used != 0u)
            {
                used += g_gc.ent[i].bytes;
                n++;
            }
        }
        if (used_bytes != NULL)
            *used_bytes = used;
        if (entries != NULL)
            *entries = n;
    }
}
