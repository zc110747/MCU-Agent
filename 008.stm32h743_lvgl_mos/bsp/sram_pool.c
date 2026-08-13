/**
  ******************************************************************************
  * @file    sram_pool.c
  * @brief   Dynamic allocator over DTCM and RAM_D2.  See sram_pool.h.
  ******************************************************************************
  */
#include "sram_pool.h"

#include <string.h>

/* -------------------------------------------------------------------------- */
/*  Block layout                                                              */
/*                                                                             */
/*    +------------------+ <-- block start (8-aligned)                        */
/*    | header (8 B)     |     size_in_bytes | USED_BIT                       */
/*    +------------------+ <-- payload (returned to caller, 8-aligned)        */
/*    | ... payload ...  |                                                    */
/*    +------------------+ <-- footer (8 B) at block_end - 8                   */
/*    | footer (8 B)     |     size_in_bytes | USED_BIT                       */
/*    +------------------+                                                    */
/*                                                                             */
/*  The header and footer carry the *same* word, so a free block can be       */
/*  coalesced with its neighbour in either direction in O(1).                 */
/* -------------------------------------------------------------------------- */

#define BLK_META   (2U * sizeof(uint32_t))   /* header + footer = 16 bytes  */
#define MIN_BLK    (2U * BLK_META)           /* smallest block = 32 bytes   */
#define USED_BIT    1U

typedef uint32_t blk_t;       /* the 32-bit size|used word */

typedef struct
{
    uint32_t base;            /* region start, 8-byte aligned */
    uint32_t total;           /* region size, multiple of 8  */
} region_def_t;

static const region_def_t REGIONS[SRAM_REGION_COUNT] =
{
    [SRAM_REGION_DTCM] = { 0x20000000U, 128U * 1024U },
    [SRAM_REGION_D2]   = { 0x30000000U, 288U * 1024U },
};

/* The region base is taken through this helper so the same code can be
 * compiled on a PC (SRAM_POOL_HOST_TEST) against a malloc'd backing buffer
 * for fuzzing, instead of the physical 0x20000000 / 0x30000000 addresses. */
#ifdef SRAM_POOL_HOST_TEST
static uint8_t *s_host_mem = NULL;
void sram_pool_host_attach(uint8_t *mem) { s_host_mem = mem; }
static inline uint8_t *sram_pool_base(int r)
{
    /* DTCM at offset 0, D2 at offset 512 KiB within the single host buffer. */
    return s_host_mem + (r == SRAM_REGION_D2 ? (512U * 1024U) : 0U);
}
#else
static inline uint8_t *sram_pool_base(int r)
{
    return (uint8_t *)REGIONS[r].base;
}
#endif

static int s_inited = 0;

/* ---- block primitives --------------------------------------------------- */

static inline uint32_t blk_size(blk_t *h)      { return *h & ~USED_BIT;      }
static inline int      blk_used(blk_t *h)      { return (*h & USED_BIT) != 0U; }

static inline blk_t *blk_next(blk_t *h)        { return (blk_t *)((uint8_t *)h + blk_size(h)); }
static inline blk_t *blk_prev(blk_t *h)
{
    /* the previous block's footer is the 8 bytes just before this header */
    blk_t *ftr = (blk_t *)((uint8_t *)h - BLK_META);
    return (blk_t *)((uint8_t *)h - blk_size(ftr));
}
static inline blk_t *blk_ftr(blk_t *h)         { return (blk_t *)((uint8_t *)h + blk_size(h) - BLK_META); }
static inline blk_t *blk_from_payload(void *p) { return (blk_t *)((uint8_t *)p - BLK_META); }

/* -------------------------------------------------------------------------- */

void sram_pool_init(void)
{
    for (int r = 0; r < SRAM_REGION_COUNT; r++)
    {
        uint8_t *base  = sram_pool_base(r);
        uint32_t total = REGIONS[r].total;

        /* Prologue: a used block of MIN_BLK anchoring the very front. Its
         * footer sits at base + MIN_BLK - BLK_META, so the first real block
         * starts at base + MIN_BLK. */
        blk_t *pro = (blk_t *)base;
        *pro = (MIN_BLK | USED_BIT);
        *(blk_t *)((uint8_t *)pro + MIN_BLK - BLK_META) = (MIN_BLK | USED_BIT);

        /* One big free block between the prologue and the epilogue. */
        blk_t  *fb   = (blk_t *)(base + MIN_BLK);
        uint32_t fbsz = (total - MIN_BLK - BLK_META) & ~(BLK_META - 1U);
        *fb = fbsz;                                  /* free */
        *(blk_t *)((uint8_t *)fb + fbsz - BLK_META) = fbsz;

        /* Epilogue: a used 0-size block that terminates the scan. */
        blk_t *epi = (blk_t *)((uint8_t *)fb + fbsz);
        *epi = (0U | USED_BIT);
    }

    s_inited = 1;
}

void *sram_alloc(sram_region_t region, size_t size, size_t align)
{
    if (!s_inited)
    {
        sram_pool_init();
    }
    if ((region >= SRAM_REGION_COUNT) || (size == 0U))
    {
        return NULL;
    }

    /* Payload is always 8-byte aligned, which satisfies every current caller
     * (NES asks for 4/8).  The parameter is kept in the API so a future app
     * (e.g. a camera DMA framebuffer) can request stricter alignment without
     * changing the call sites. */
    (void)align;

    uint32_t need = (uint32_t)size + 2U * BLK_META;           /* payload + header + footer */
    need = (need + (BLK_META - 1U)) & ~(BLK_META - 1U);       /* round up to 16   */
    if (need < MIN_BLK)
    {
        need = MIN_BLK;
    }

    blk_t *h = (blk_t *)sram_pool_base((int)region);

    for (;;)
    {
        if (blk_size(h) == 0U)             /* epilogue: scan exhausted */
        {
            return NULL;
        }

        uint32_t bs = blk_size(h);

        if (!blk_used(h) && (bs >= need))
        {
            if ((bs - need) >= MIN_BLK)    /* split off a free remainder */
            {
                blk_t  *nb   = (blk_t *)((uint8_t *)h + need);
                uint32_t nbsz = bs - need;

                *nb = nbsz;                                  /* remainder free */
                *(blk_t *)((uint8_t *)nb + nbsz - BLK_META) = nbsz;

                *h = (need | USED_BIT);
                *(blk_t *)((uint8_t *)h + need - BLK_META) = (need | USED_BIT);
            }
            else                           /* use the whole block */
            {
                *h |= USED_BIT;
                *(blk_t *)((uint8_t *)h + bs - BLK_META) |= USED_BIT;
            }
            return (uint8_t *)h + BLK_META;
        }

        h = blk_next(h);
    }
}

void sram_free(sram_region_t region, void *ptr)
{
    (void)region;

    if (ptr == NULL)
    {
        return;
    }

    blk_t   *h  = blk_from_payload(ptr);
    uint32_t bs = blk_size(h);

    /* Defensive: a block already marked free must not be freed again.  A real
     * caller never double-frees, but this guards against a stray/merged
     * pointer (e.g. a stress harness holding a pointer that got absorbed by a
     * neighbour during coalescing) turning one bad call into heap-wide
     * corruption. */
    if (!blk_used(h))
    {
        return;
    }

    *h &= ~USED_BIT;                                   /* mark free */
    *(blk_t *)((uint8_t *)h + bs - BLK_META) &= ~USED_BIT;

    /* Coalesce with the next block (skip the epilogue, size 0). */
    blk_t *nxt = blk_next(h);
    if (!blk_used(nxt) && (blk_size(nxt) != 0U))
    {
        uint32_t ns = blk_size(nxt);
        bs += ns;
        *h = bs;
        *(blk_t *)((uint8_t *)h + bs - BLK_META) = bs;
        nxt = blk_next(h);
    }

    /* Coalesce with the previous block (the prologue is used, so safe). */
    blk_t *prev = blk_prev(h);
    if (!blk_used(prev))
    {
        uint32_t ps = blk_size(prev);
        uint32_t total = ps + bs;
        *prev = total;
        *(blk_t *)((uint8_t *)prev + total - BLK_META) = total;
        h = prev;
    }

    (void)nxt;
}

size_t sram_free_bytes(sram_region_t region)
{
    if (!s_inited || (region >= SRAM_REGION_COUNT))
    {
        return 0U;
    }

    size_t   free = 0U;
    blk_t   *h    = (blk_t *)sram_pool_base((int)region);

    for (;;)
    {
        if (blk_size(h) == 0U)            /* epilogue */
        {
            break;
        }
        if (!blk_used(h))
        {
            free += (size_t)(blk_size(h) - 2U * BLK_META);
        }
        h = blk_next(h);
    }

    return free;
}

size_t sram_total_bytes(sram_region_t region)
{
    if (region >= SRAM_REGION_COUNT)
    {
        return 0U;
    }
    return (size_t)REGIONS[region].total;
}

int sram_walk(sram_region_t region, void *out[], int max)
{
    int n = 0;

    if (region >= SRAM_REGION_COUNT)
    {
        return 0;
    }

    /* Skip the prologue, then list every used block's payload. */
    blk_t *h = (blk_t *)sram_pool_base((int)region);
    h = blk_next(h);                                  /* jump over prologue */

    for (;;)
    {
        uint32_t bs = blk_size(h);
        if (bs == 0U)                                /* epilogue */
        {
            break;
        }
        if (blk_used(h) && (bs >= MIN_BLK))
        {
            if (n < max)
            {
                out[n] = (uint8_t *)h + BLK_META;
            }
            n++;
        }
        h = blk_next(h);
    }

    return n;
}

/* -------------------------------------------------------------------------- */
/*  Integrity check + randomized stress                                       */
/* -------------------------------------------------------------------------- */

size_t sram_check(sram_region_t region, int *ok)
{
    int    rc   = 1;
    size_t free = 0U;

    if (ok != NULL)
    {
        *ok = 0;
    }
    if (region >= SRAM_REGION_COUNT)
    {
        return 0U;
    }

    uint8_t  *base  = sram_pool_base((int)region);
    uint32_t  total = REGIONS[region].total;

    blk_t *h = (blk_t *)base;

    for (;;)
    {
        uint32_t bs = blk_size(h);

        if (bs == 0U)                       /* epilogue: a 0-size block with
                                             * NO footer - just verify it sits
                                             * exactly at the end of the arena. */
        {
            if ((uint8_t *)h != (base + total - BLK_META))
            {
                rc = 0;
            }
            break;
        }

        /* header/footer must agree */
        blk_t *ftr = (blk_t *)((uint8_t *)h + bs - BLK_META);
        if (*ftr != *h)
        {
            rc = 0;
            break;
        }
        if (!blk_used(h))
        {
            free += (size_t)(bs - 2U * BLK_META);
        }
        h = blk_next(h);
    }

    if (ok != NULL)
    {
        *ok = rc;
    }
    return free;
}

/* Small deterministic PRNG (xorshift32) so a stress run is reproducible. */
static uint32_t s_xor32(uint32_t *s)
{
    uint32_t x = *s;
    x ^= (x << 13);
    x ^= (x >> 17);
    x ^= (x << 5);
    *s = x;
    return x;
}

int sram_stress_test(sram_region_t region, uint32_t iters, uint32_t seed)
{
    if (region >= SRAM_REGION_COUNT)
    {
        return 1;
    }

    uint32_t total  = REGIONS[region].total;
    uint32_t maxsz  = total / 8U;                 /* many small blocks, not one giant */
    if (maxsz < (MIN_BLK * 2U))
    {
        maxsz = MIN_BLK * 2U;
    }

    /* The stress only owns blocks it allocated here, so it must start from a
     * clean pool (e.g. no NES emulator open).  Callers guarantee that. */
    void   *ptrs[256];
    uint32_t s = (seed != 0U) ? seed : 0x9E3779B9U;

    /* Free payload of a clean pool - the value the sweep must restore. */
    size_t empty_free = sram_free_bytes(region);

    int corrupt_at = 0;

    for (uint32_t i = 0U; i < iters; i++)
    {
        /* Re-walk the live set every iteration.  This is what keeps the test
         * correct: after any free coalesces neighbours, the previous pointers
         * may now sit inside a merged block, so we never trust a stale one. */
        int n = sram_walk(region, ptrs, 256);

        /* Prefer freeing once the live set grows, to bound it under 256 and
         * keep a healthy mix of alloc/free pressure. */
        int do_alloc = (n == 0) ? 1 : (n >= 200 ? 0 : ((int)(s_xor32(&s) & 1U)));

        if (do_alloc)
        {
            uint32_t sz = MIN_BLK + (s_xor32(&s) % (maxsz - MIN_BLK + 1U));
            (void)sram_alloc(region, sz, 8U);     /* success/failure both fine */
        }
        else
        {
            int idx = (int)(s_xor32(&s) % (uint32_t)n);
            sram_free(region, ptrs[idx]);          /* ptrs[idx] is guaranteed live */
        }

        /* Integrity must hold after every single op. */
        int okf = 1;
        sram_check(region, &okf);
        if (!okf)
        {
            corrupt_at = (int)(i + 1U);
            break;
        }
    }

    /* Sweep: free every live block (re-walked, so no double-free). */
    int n = sram_walk(region, ptrs, 256);
    while (n-- > 0)
    {
        sram_free(region, ptrs[n]);
    }

    int    okf  = 1;
    size_t free = sram_check(region, &okf);
    if (!okf)
    {
        return 3;                               /* corruption after sweep */
    }
    if (corrupt_at != 0)
    {
        return 2;                               /* corruption at op N */
    }
    if (free != empty_free)
    {
        return 4;                               /* leaked / not coalesced */
    }
    return 0;                                   /* PASS */
}
