/**
  ******************************************************************************
  * @file    ttf_reader.h
  * @brief   Random-access reader for a TTF file that lives on the SD card.
  *
  *  This is the only place in the font stack that is allowed to talk to FatFs.
  *  stb_truetype must never call f_lseek()/f_read() itself - it goes through
  *  the stb adapter, which calls here, which goes through the block cache.
  *
  *  Guarantees
  *  ----------
  *   - Every read is bounds checked against the file size.  The check is
  *     written as two comparisons (offset > size, then len > size - offset)
  *     specifically so "offset + length" can never overflow and wrap back
  *     into a valid-looking range.
  *   - The whole file is never buffered.  Only block_count * block_size bytes
  *     are held, supplied by the caller.
  ******************************************************************************
  */
#ifndef __TTF_READER_H
#define __TTF_READER_H

#include <stdint.h>
#include "main.h"
#include "ff.h"
#include "blkcache.h"

/** Defaults: 16 KB x 4 = 64 KB, the configuration the spec asks for. */
#define TTF_BLOCK_SIZE    (16u * 1024u)
#define TTF_BLOCK_COUNT   4u

typedef struct
{
    FIL         f;
    uint8_t     open;
    uint32_t    size;         /**< file size in bytes                        */
    blkcache_t  cache;
    uint32_t    seeks;        /**< backing-store seeks issued (misses)       */
    uint32_t    reads;        /**< f_read() calls issued                     */
    uint32_t    bytes;        /**< bytes pulled from the card                */
    uint32_t    cyc_seek;     /**< cycles spent in f_lseek() on misses       */
    uint32_t    cyc_read;     /**< cycles spent in f_read() on misses        */
} ttf_reader_t;

/**
  * @brief  Open a TTF (or any file) for cached random access.
  * @param  r            reader object
  * @param  path         FatFs path, e.g. "1:/SYSTEM/HarmonyOS_Sans_SC/X.ttf"
  * @param  cache_buf    block_size * block_count bytes, owned by the caller
  * @param  block_size   power of two, e.g. TTF_BLOCK_SIZE
  * @param  block_count  <= BLKCACHE_MAX_BLOCKS
  * @retval RT_OK on success
  */
GlobalType_t ttf_open(ttf_reader_t *r,
                      const char   *path,
                      uint8_t      *cache_buf,
                      uint32_t      block_size,
                      uint32_t      block_count);

/** Close the file and invalidate the cache. */
void ttf_close(ttf_reader_t *r);

/** File size in bytes. */
uint32_t ttf_size(const ttf_reader_t *r);

/**
  * @brief  Read @p len bytes at @p offset.
  * @retval RT_FAIL when the reader is closed or the range leaves the file.
  */
GlobalType_t ttf_read(ttf_reader_t *r, uint32_t offset, void *dst, uint32_t len);

/** Big-endian convenience readers for TrueType tables. */
GlobalType_t ttf_read_u16(ttf_reader_t *r, uint32_t offset, uint16_t *v);
GlobalType_t ttf_read_u32(ttf_reader_t *r, uint32_t offset, uint32_t *v);

/** Drop cached blocks without closing the file. */
void ttf_cache_flush(ttf_reader_t *r);

void ttf_stats(const ttf_reader_t *r,
               uint32_t *hits,  uint32_t *misses,
               uint32_t *fills, uint32_t *fill_bytes);

/**
  * @brief  Cycles spent inside the two blocking calls, seek and read.
  *
  *  This is how the cost of a cold glyph is split between the card and the
  *  rasteriser.  Returns 0 when there is no cycle counter (host builds).
  */
void ttf_cycles(const ttf_reader_t *r, uint32_t *seek, uint32_t *read);

#endif /* __TTF_READER_H */
