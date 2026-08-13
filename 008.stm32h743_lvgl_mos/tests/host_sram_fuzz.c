/*
 * host_sram_fuzz.c - PC fuzz harness for bsp/sram_pool.c.
 *
 * Compiles the REAL allocator (via #include of sram_pool.c) against a
 * malloc'd backing buffer and runs millions of randomized alloc/free ops,
 * asserting allocator invariants after every step.  Built with
 * -fsanitize=address so any header/footer overrun is caught instantly.
 *
 * Build (mingw gcc):
 *   gcc -DSRAM_POOL_HOST_TEST -g -O1 -I../bsp host_sram_fuzz.c -o host_sram_fuzz
 * Run:
 *   ./host_sram_fuzz
 *
 * (ASan is not shipped with this mingw toolchain, so instead we assert the
 *  allocator's returned payload is large enough and rely on sram_check's
 *  structural invariants + full-recovery at the end.)
 */
#ifndef SRAM_POOL_HOST_TEST
#define SRAM_POOL_HOST_TEST
#endif
#include "../bsp/sram_pool.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* xorshift32, mirrors sram_pool.c's PRNG so both share the same sequence. */
static uint32_t xor32(uint32_t *s)
{
    uint32_t x = *s;
    x ^= (x << 13);
    x ^= (x >> 17);
    x ^= (x << 5);
    *s = x;
    return x;
}

#define LIVE_MAX 1024

int main(void)
{
    /* One 1 MiB buffer; DTCM at offset 0, D2 at offset 512 KiB. */
    size_t bufsz = 1024UL * 1024UL;
    uint8_t *mem = (uint8_t *)malloc(bufsz);
    if (!mem)
    {
        fprintf(stderr, "malloc failed\n");
        return 2;
    }
    memset(mem, 0xAA, bufsz);
    sram_pool_host_attach(mem);
    sram_pool_init();

    size_t empty_dtcm = sram_free_bytes(SRAM_REGION_DTCM);
    size_t empty_d2   = sram_free_bytes(SRAM_REGION_D2);

    void *live[LIVE_MAX];
    uint32_t seed = 0x12345678U;
    long total_ops = 0;
    int failures = 0;

    for (int region = 0; region < SRAM_REGION_COUNT; region++)
    {
        uint32_t total = REGIONS[region].total;
        uint32_t maxsz = total / 8U;
        if (maxsz < (MIN_BLK * 2U)) maxsz = MIN_BLK * 2U;

        for (int pass = 0; pass < 40; pass++)
        {
            /* many iterations per pass */
            for (int it = 0; it < 200000; it++)
            {
                int n = sram_walk((sram_region_t)region, live, LIVE_MAX);
                int do_alloc = (n == 0) ? 1 : (n >= 800 ? 0 : ((int)(xor32(&seed) & 1U)));

                if (do_alloc)
                {
                    uint32_t sz = MIN_BLK + (xor32(&seed) % (maxsz - MIN_BLK + 1U));
                    void *p = sram_alloc((sram_region_t)region, sz, 8U);
                    /* Deliberately write the full payload to catch overruns. */
                    if (p != NULL)
                    {
                        /* The allocator MUST return at least `sz` usable bytes:
                         * payload = block size minus the 8-byte header.  This is
                         * exactly the property the off-by-8 bug broke. */
                        uint32_t blk = blk_size(blk_from_payload(p));
                        if ((blk - BLK_META) < sz)
                        {
                            fprintf(stderr, "UNDERALLOC region=%d sz=%u got=%u\n",
                                    region, sz, blk - BLK_META);
                            failures++;
                            goto done_region;
                        }
                        memset(p, 0x5A, sz);
                    }
                }
                else
                {
                    int idx = (int)(xor32(&seed) % (uint32_t)n);
                    sram_free((sram_region_t)region, live[idx]);
                }
                total_ops++;

                int ok = 1;
                sram_check((sram_region_t)region, &ok);
                if (!ok)
                {
                    fprintf(stderr, "CORRUPTION region=%d pass=%d op=%d\n",
                            region, pass, it);
                    failures++;
                    goto done_region;
                }
            }

            /* Between passes, confirm full recovery. */
            int n = sram_walk((sram_region_t)region, live, LIVE_MAX);
            while (n-- > 0)
            {
                sram_free((sram_region_t)region, live[n]);
            }
            int ok = 1;
            size_t free_now = sram_check((sram_region_t)region, &ok);
            size_t empty = (region == SRAM_REGION_DTCM) ? empty_dtcm : empty_d2;
            if (!ok || (free_now != empty))
            {
                fprintf(stderr, "NOT RECOVERED region=%d pass=%d free=%lu empty=%lu ok=%d\n",
                        region, pass, (unsigned long)free_now, (unsigned long)empty, ok);
                failures++;
                goto done_region;
            }
        }
    }

done_region:
    if (failures == 0)
    {
        printf("FUZZ PASS: %ld ops, allocator invariants held, full recovery verified.\n",
               total_ops);
        return 0;
    }
    printf("FUZZ FAIL: %d failure(s) after %ld ops.\n", failures, total_ops);
    return 1;
}
