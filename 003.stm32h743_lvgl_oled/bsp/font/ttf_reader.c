/**
  ******************************************************************************
  * @file    ttf_reader.c
  * @brief   Implementation of the cached TTF reader - see ttf_reader.h.
  ******************************************************************************
  */
#include "ttf_reader.h"
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Backing store: the only f_lseek()/f_read() pair in the font stack          */
/* -------------------------------------------------------------------------- */

/* The cycle counter is a Cortex-M debug peripheral: present on the target,
 * absent in the PC-side host test.  Both builds compile this file, so the
 * timing code has to disappear cleanly rather than be stubbed per-platform. */
#if defined(DWT) && defined(CoreDebug)
    #define TTF_HAVE_CYCLES 1
#endif

#ifdef TTF_HAVE_CYCLES
static void ttf_cycles_start(void)
{
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0u)
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0u;
        DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
    }
}
#endif

static GlobalType_t ttf_fill(void     *ctx,
                             uint32_t  offset,
                             uint8_t  *dst,
                             uint32_t  len,
                             uint32_t *got)
{
    ttf_reader_t *r = (ttf_reader_t *)ctx;
    UINT          br = 0u;
#ifdef TTF_HAVE_CYCLES
    uint32_t      c0;
#endif

#ifdef TTF_HAVE_CYCLES
    c0 = DWT->CYCCNT;
#endif
    if (f_lseek(&r->f, (FSIZE_t)offset) != FR_OK)
    {
        return RT_FAIL;
    }
#ifdef TTF_HAVE_CYCLES
    r->cyc_seek += DWT->CYCCNT - c0;
    c0 = DWT->CYCCNT;
#endif

    if (f_read(&r->f, dst, len, &br) != FR_OK)
    {
        return RT_FAIL;
    }
#ifdef TTF_HAVE_CYCLES
    r->cyc_read += DWT->CYCCNT - c0;
#endif

    r->seeks++;
    r->reads++;
    r->bytes += (uint32_t)br;

    *got = (uint32_t)br;
    return RT_OK;
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

GlobalType_t ttf_open(ttf_reader_t *r,
                      const char   *path,
                      uint8_t      *cache_buf,
                      uint32_t      block_size,
                      uint32_t      block_count)
{
    FRESULT res;

    r->open  = 0u;
    r->size  = 0u;
    r->seeks = 0u;
    r->reads = 0u;
    r->bytes = 0u;

#ifdef TTF_HAVE_CYCLES
    r->cyc_seek = 0u;
    r->cyc_read = 0u;
    ttf_cycles_start();
#endif

    if (r == NULL || path == NULL || cache_buf == NULL)
    {
        return RT_FAIL;
    }

    res = f_open(&r->f, path, FA_READ);
    if (res != FR_OK)
    {
        return RT_FAIL;
    }

    r->size = (uint32_t)f_size(&r->f);
    blkcache_init(&r->cache, cache_buf, block_size, block_count, r, ttf_fill);
    r->open = 1u;
    return RT_OK;
}

void ttf_close(ttf_reader_t *r)
{
    if (r == NULL)
    {
        return;
    }
    if (r->open != 0u)
    {
        (void)f_close(&r->f);
    }
    blkcache_flush(&r->cache);
    r->open = 0u;
    r->size = 0u;
}

uint32_t ttf_size(const ttf_reader_t *r)
{
    return (r != NULL) ? r->size : 0u;
}

GlobalType_t ttf_read(ttf_reader_t *r, uint32_t offset, void *dst, uint32_t len)
{
    if (r == NULL || r->open == 0u)
    {
        return RT_FAIL;
    }
    if (len == 0u)
    {
        return RT_OK;
    }
    if (dst == NULL)
    {
        return RT_FAIL;
    }

    /*
     * Two comparisons on purpose.  "offset + len > size" can overflow for a
     * corrupt offset and wrap back into range, letting a wild read through.
     */
    if (offset > r->size)
    {
        return RT_FAIL;
    }
    if (len > (r->size - offset))
    {
        return RT_FAIL;
    }

    return blkcache_read(&r->cache, offset, (uint8_t *)dst, len);
}

GlobalType_t ttf_read_u16(ttf_reader_t *r, uint32_t offset, uint16_t *v)
{
    uint8_t b[2];

    if (v == NULL)
    {
        return RT_FAIL;
    }
    /* TrueType is big-endian. */
    if (ttf_read(r, offset, b, sizeof(b)) != RT_OK)
    {
        return RT_FAIL;
    }
    *v = (uint16_t)(((uint16_t)b[0] << 8) | (uint16_t)b[1]);
    return RT_OK;
}

GlobalType_t ttf_read_u32(ttf_reader_t *r, uint32_t offset, uint32_t *v)
{
    uint8_t b[4];

    if (v == NULL)
    {
        return RT_FAIL;
    }
    if (ttf_read(r, offset, b, sizeof(b)) != RT_OK)
    {
        return RT_FAIL;
    }
    *v = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
         ((uint32_t)b[2] << 8)  | (uint32_t)b[3];
    return RT_OK;
}

void ttf_cache_flush(ttf_reader_t *r)
{
    if (r != NULL)
    {
        blkcache_flush(&r->cache);
    }
}

void ttf_stats(const ttf_reader_t *r,
               uint32_t *hits,  uint32_t *misses,
               uint32_t *fills, uint32_t *fill_bytes)
{
    if (r == NULL)
    {
        return;
    }
    blkcache_stats(&r->cache, hits, misses, fills, fill_bytes);
}

void ttf_cycles(const ttf_reader_t *r, uint32_t *seek, uint32_t *read)
{
    if (seek != NULL)
    {
#ifndef TTF_HAVE_CYCLES
        *seek = 0u;
#else
        *seek = (r != NULL) ? r->cyc_seek : 0u;
#endif
    }
    if (read != NULL)
    {
#ifndef TTF_HAVE_CYCLES
        *read = 0u;
#else
        *read = (r != NULL) ? r->cyc_read : 0u;
#endif
    }
}
