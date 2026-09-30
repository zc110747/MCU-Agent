/**
  ******************************************************************************
  * @file    blkcache.h
  * @brief   Small LRU block cache that turns random small reads into few big ones.
  *
  *  stb_truetype in stream mode reads a font a byte at a time: every table it
  *  visits (cmap / loca / glyf / hmtx / GPOS) turns into a seek plus a 1-byte
  *  read, and a multi-MB TTF on an SD card turns that into thousands of
  *  transactions per glyph.  This cache sits in front of the file and hands
  *  out whole blocks instead.
  *
  *  The cache is deliberately generic: it knows nothing about FatFs.  The owner
  *  supplies the backing buffer at init time and a fill callback that pulls one
  *  block from wherever the data really lives.  That way the same code serves
  *  the TTF (16 KB blocks) and the CTF index (512 B blocks) without the TTF
  *  cache ever evicting the index it depends on.
  *
  *  No malloc: the block buffer and every bookkeeping array are supplied or
  *  fixed at compile time.
  ******************************************************************************
  */
#ifndef __BLKCACHE_H
#define __BLKCACHE_H

#include <stdint.h>
#include "main.h"

/** Hard cap on blocks, so the tag/age bookkeeping stays a fixed-size array. */
#define BLKCACHE_MAX_BLOCKS  8u

/** Marks an empty slot (a real block number is always < 0x80000000 here). */
#define BLKCACHE_TAG_INVALID 0xFFFFFFFFu

struct blkcache_t;

/**
  * @brief  Pull one block from the backing store.
  * @param  ctx     opaque owner context (a FIL*, say)
  * @param  offset  absolute byte offset to read from
  * @param  dst     destination, @p len bytes
  * @param  len     block size in bytes
  * @param  got     out: bytes actually read (short read at EOF is normal)
  * @retval RT_OK on success
  */
typedef GlobalType_t (*blkcache_fill_cb)(void     *ctx,
                                         uint32_t  offset,
                                         uint8_t  *dst,
                                         uint32_t  len,
                                         uint32_t *got);

typedef struct blkcache_t
{
    uint8_t          *buf;                        /**< block_count * block_size */
    uint32_t          block_size;                 /**< power of two            */
    uint32_t          block_shift;                /**< log2(block_size)        */
    uint32_t          block_count;                /**< <= BLKCACHE_MAX_BLOCKS  */

    uint32_t          tag[BLKCACHE_MAX_BLOCKS];   /**< block number per slot   */
    uint32_t          len[BLKCACHE_MAX_BLOCKS];   /**< valid bytes in the slot */
    uint32_t          age[BLKCACHE_MAX_BLOCKS];   /**< LRU clock stamp         */
    uint32_t          clock;

    void             *ctx;
    blkcache_fill_cb  fill;

    /* Counters, for tuning block size / count. */
    uint32_t          hits;
    uint32_t          misses;
    uint32_t          fills;      /**< times the backing store was touched   */
    uint32_t          fill_bytes;
} blkcache_t;

/**
  * @brief  Initialise (and invalidate) a cache.
  * @param  c            cache object
  * @param  buf          backing memory, block_size * block_count bytes
  * @param  block_size   must be a power of two
  * @param  block_count  must be <= BLKCACHE_MAX_BLOCKS and >= 1
  * @param  ctx          passed straight to @p fill
  * @param  fill         backing-store reader
  */
void blkcache_init(blkcache_t      *c,
                   uint8_t         *buf,
                   uint32_t         block_size,
                   uint32_t         block_count,
                   void            *ctx,
                   blkcache_fill_cb fill);

/**
  * @brief  Read @p len bytes at @p offset, crossing block boundaries as needed.
  *
  *  The caller bounds-checks against the file size first; this only guarantees
  *  it will not read past the end of a short (EOF) block.
  *
  * @retval RT_OK, or RT_FAIL when the backing store fails or the offset falls
  *         past the last valid byte of its block.
  */
GlobalType_t blkcache_read(blkcache_t *c, uint32_t offset,
                           uint8_t *dst, uint32_t len);

/** Drop every cached block (call after seeking the underlying file elsewhere). */
void blkcache_flush(blkcache_t *c);

void blkcache_stats(const blkcache_t *c,
                    uint32_t *hits,    uint32_t *misses,
                    uint32_t *fills,   uint32_t *fill_bytes);

#endif /* __BLKCACHE_H */
