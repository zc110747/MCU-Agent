/**
  ******************************************************************************
  * @file    blkcache.c
  * @brief   Implementation of the LRU block cache - see blkcache.h.
  ******************************************************************************
  */
#include "blkcache.h"
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Slot management                                                            */
/* -------------------------------------------------------------------------- */

static int32_t find_slot(const blkcache_t *c, uint32_t block)
{
    uint32_t i;
    for (i = 0u; i < c->block_count; i++)
    {
        if (c->tag[i] == block)
        {
            return (int32_t)i;
        }
    }
    return -1;
}

/**
  * Pick the slot to reuse: an empty one if there is any, otherwise the least
  * recently used.  Ties (only possible before the clock wraps, or after a
  * flush) resolve to the lowest index.
  */
static uint32_t victim_slot(blkcache_t *c)
{
    uint32_t i;
    uint32_t best = 0u;
    uint32_t best_age = 0xFFFFFFFFu;
    int      found = 0;

    for (i = 0u; i < c->block_count; i++)
    {
        if (c->tag[i] == BLKCACHE_TAG_INVALID)
        {
            return i;
        }
        if (!found || c->age[i] < best_age)
        {
            best_age = c->age[i];
            best     = i;
            found    = 1;
        }
    }
    return best;
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

void blkcache_init(blkcache_t      *c,
                   uint8_t         *buf,
                   uint32_t         block_size,
                   uint32_t         block_count,
                   void            *ctx,
                   blkcache_fill_cb fill)
{
    uint32_t i;
    uint32_t shift = 0u;
    uint32_t probe = block_size;

    if (block_count > BLKCACHE_MAX_BLOCKS)
    {
        block_count = BLKCACHE_MAX_BLOCKS;
    }
    if (block_count == 0u)
    {
        block_count = 1u;
    }

    while (probe > 1u)
    {
        probe >>= 1;
        shift++;
    }

    c->buf         = buf;
    c->block_size  = block_size;
    c->block_shift = shift;
    c->block_count = block_count;
    c->clock       = 0u;
    c->ctx         = ctx;
    c->fill        = fill;
    c->hits        = 0u;
    c->misses      = 0u;
    c->fills       = 0u;
    c->fill_bytes  = 0u;

    for (i = 0u; i < BLKCACHE_MAX_BLOCKS; i++)
    {
        c->tag[i] = BLKCACHE_TAG_INVALID;
        c->len[i] = 0u;
        c->age[i] = 0u;
    }
}

GlobalType_t blkcache_read(blkcache_t *c, uint32_t offset,
                           uint8_t *dst, uint32_t len)
{
    while (len > 0u)
    {
        uint32_t  block = offset >> c->block_shift;
        uint32_t  in_blk = offset & (c->block_size - 1u);
        int32_t   slot;
        uint32_t  chunk;

        slot = find_slot(c, block);
        if (slot < 0)
        {
            uint32_t got = 0u;

            slot = victim_slot(c);
            if (c->fill(c->ctx, block << c->block_shift,
                        c->buf + ((uint32_t)slot * c->block_size),
                        c->block_size, &got) != RT_OK)
            {
                c->tag[slot] = BLKCACHE_TAG_INVALID;
                c->len[slot] = 0u;
                return RT_FAIL;
            }
            c->tag[slot] = block;
            c->len[slot] = got;
            c->misses++;
            c->fills++;
            c->fill_bytes += got;
        }
        else
        {
            c->hits++;
        }

        c->clock++;
        c->age[slot] = c->clock;

        /* Short block at EOF: reading past its valid bytes is a hard error. */
        if (in_blk >= c->len[slot])
        {
            return RT_FAIL;
        }

        chunk = c->len[slot] - in_blk;
        if (chunk > len)
        {
            chunk = len;
        }
        (void)memcpy(dst, c->buf + ((uint32_t)slot * c->block_size) + in_blk, chunk);

        offset += chunk;
        dst    += chunk;
        len    -= chunk;
    }
    return RT_OK;
}

void blkcache_flush(blkcache_t *c)
{
    uint32_t i;
    for (i = 0u; i < BLKCACHE_MAX_BLOCKS; i++)
    {
        c->tag[i] = BLKCACHE_TAG_INVALID;
        c->len[i] = 0u;
        c->age[i] = 0u;
    }
}

void blkcache_stats(const blkcache_t *c,
                    uint32_t *hits,  uint32_t *misses,
                    uint32_t *fills, uint32_t *fill_bytes)
{
    if (hits)       { *hits       = c->hits; }
    if (misses)     { *misses     = c->misses; }
    if (fills)      { *fills      = c->fills; }
    if (fill_bytes) { *fill_bytes = c->fill_bytes; }
}
