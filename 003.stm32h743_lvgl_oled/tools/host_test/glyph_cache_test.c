/**
 * Host-side unit test for the TTF Glyph Cache (glyph_cache.c).
 *
 * Compiles the REAL firmware source against the host toolchain - no FatFs /
 * LVGL / stb shim needed, glyph_cache.c only depends on glyph_cache.h and
 * string.h.  This validates the LRU eviction, lookup-promotion and page-epoch
 * pinning logic that the 2-page on-target demo cannot reach (it never fills
 * the 160 KB pool, so s_evicts stays 0 on hardware).
 *
 * Key model (matches the spec): a glyph is only evictable when it belongs to a
 * NON-current epoch (an old page).  Within the current page (single epoch) the
 * cache never reclaims its own pinned glyphs - it returns NULL on overfill,
 * which the draw path treats as "skip this glyph" rather than deadlock.
 *
 * Build:
 *   gcc -std=c11 -O2 -Wall -Wextra -I <prj>/Bsp/font \
 *       glyph_cache_test.c <prj>/Bsp/font/glyph_cache.c -o glyph_cache_test.exe
 */
#include "glyph_cache.h"
#include <stdio.h>

static int g_pass;
static int g_fail;

static void check(const char *what, int ok)
{
    if (ok)
    {
        g_pass++;
        printf("  [PASS] %s\n", what);
    }
    else
    {
        g_fail++;
        printf("  [FAIL] %s\n", what);
    }
}

/* 64x64 glyph -> 4096 bytes (already 4-aligned).  Pool (160 KB) holds 40. */
static uint8_t *ins(uint32_t cp, uint16_t px)
{
    uint16_t w = 64, h = 64;
    uint32_t bytes = 0;
    return glyph_cache_insert(cp, px, w, h, &bytes);
}

static int present(uint32_t cp, uint16_t px)
{
    uint16_t w, h;
    uint32_t b;
    return glyph_cache_lookup(cp, px, &w, &h, &b) != NULL;
}

int main(void)
{
    uint16_t w, h;
    uint32_t b, hits, misses, evicts, used, entries;

    printf("=== Glyph Cache host unit test ===\n");

    /* ---- Scenario A: mirrors the real draw path (lookup miss -> insert -> hit) ---- */
    printf("[A] draw-path: lookup miss -> insert -> lookup hit\n");
    glyph_cache_init();
    check("lookup before insert misses (NULL)",
          glyph_cache_lookup(0x4E00, 24, &w, &h, &b) == NULL);
    uint8_t *pa = glyph_cache_insert(0x4E00, 24, 20, 20, &b);
    check("insert returns a buffer", pa != NULL);
    check("inserted bytes == 400 (20x20, 4-aligned)", b == 400);
    const uint8_t *qa = glyph_cache_lookup(0x4E00, 24, &w, &h, &b);
    check("lookup hit returns same buffer", qa == pa);
    check("lookup dims w==20", w == 20);
    check("lookup dims h==20", h == 20);
    glyph_cache_stats(&hits, &misses, &evicts, &used, &entries);
    check("stats: hits>=1 (hit) and misses>=1 (first lookup)", hits >= 1 && misses >= 1);
    check("stats: entries==1", entries == 1);
    check("stats: used==400", used == 400);

    /* ---- Scenario B: single-epoch safety - current page glyphs are pinned,
       so overfilling returns NULL instead of evicting them ---- */
    printf("[B] single-epoch overfill is safe (no eviction of pinned glyphs)\n");
    glyph_cache_init(); /* epoch 0 */
    int i;
    for (i = 0; i < 40; i++)
    {
        check("fill 40 glyphs (pool exactly full)", ins(0x4E00u + (uint32_t)i, 24) != NULL);
    }
    uint8_t *over = ins(0x4E00u + 40u, 24); /* 41st, same epoch -> no victim */
    check("41st insert returns NULL (safe, no deadlock)", over == NULL);
    glyph_cache_stats(&hits, &misses, &evicts, &used, &entries);
    check("no eviction within a single epoch", evicts == 0);
    check("all 40 pinned glyphs still present",
          present(0x4E00, 24) && present(0x4E00u + 39u, 24));

    /* ---- Scenario C: LRU promotion, observed across an epoch change ----
       A is promoted (lookup) so it survives longer than the never-accessed
       B; after bumping the epoch, new-page inserts evict the oldest old-page
       glyph (B), not the promoted one (A). */
    printf("[C] LRU promotion: min-lru old-page glyph is evicted first\n");
    glyph_cache_init(); /* epoch 0 */
    ins(0x1000, 24);                            /* A: lru 1 */
    ins(0x1001, 24);                            /* B: lru 2 */
    ins(0x1002, 24);                            /* C: lru 3 */
    glyph_cache_lookup(0x1000, 24, &w, &h, &b); /* promote A -> newest of A,B,C */
    glyph_cache_bump_epoch();                   /* -> epoch 1 (A/B/C now old page) */
    for (i = 0; i < 37; i++)
    {
        ins(0x3000u + (uint32_t)i, 24);          /* fill to 40 (pool exactly full) */
    }
    ins(0x3025, 24);                            /* 41st -> 1 eviction, victim = B */
    check("never-accessed B evicted (min lru old-page)", !present(0x1001, 24));
    check("promoted A survives", present(0x1000, 24));
    check("newest C survives", present(0x1002, 24));
    glyph_cache_stats(&hits, &misses, &evicts, &used, &entries);
    check(">=1 eviction occurred", evicts >= 1);

    /* ---- Scenario D: page-epoch pinning (current page protected) ---- */
    printf("[D] epoch pinning: old-epoch evicted, current-epoch protected\n");
    glyph_cache_init(); /* epoch 0 */
    ins(0x2000, 24);    /* A: epoch 0 */
    ins(0x2001, 24);    /* B: epoch 0 */
    glyph_cache_bump_epoch(); /* -> epoch 1 */
    ins(0x2002, 24);    /* C: epoch 1 (current page) */
    for (i = 0; i < 45; i++)
    {
        ins(0x4000u + (uint32_t)i, 24); /* fill with epoch-1 glyphs */
    }
    check("old-epoch A evicted", !present(0x2000, 24));
    check("old-epoch B evicted", !present(0x2001, 24));
    check("current-epoch C protected", present(0x2002, 24));
    glyph_cache_stats(&hits, &misses, &evicts, &used, &entries);
    check(">=2 old-epoch evictions", evicts >= 2);
    /* No current-epoch victim exists now -> insert must return NULL (draw skips),
       never deadlock or corrupt the pool. */
    uint8_t *nf = ins(0x9ABC, 24);
    check("insert with no evictable victim returns NULL (safe)", nf == NULL);
    check("current-epoch C still present after pressure", present(0x2002, 24));

    printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
