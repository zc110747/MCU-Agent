/**
 * Host-side acceptance test for the CTF font stack.
 *
 * This compiles the *firmware* sources - blkcache.c, ttf_reader.c,
 * ctf_reader.c, stb_adapter.c - unchanged, against a small FatFs/stdio shim,
 * and runs them over the real .ctf/.ttf pair.  Anything that passes here has
 * already exercised the real addressing code, the real bounds checks and the
 * real rasteriser; only the SD card driver is missing.
 *
 * Usage:
 *     ctf_host_test <font.ctf> <font.ttf>
 */
#include "ctf_reader.h"
#include "ttf_reader.h"
#include "stb_adapter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* -------------------------------------------------------------------------- */

static uint8_t ttf_cache[TTF_BLOCK_SIZE * TTF_BLOCK_COUNT];
static uint8_t ctf_cache[CTF_BLOCK_SIZE * CTF_BLOCK_COUNT];
static uint8_t l1_shadow[CTF_L1_SHADOW_SIZE];

/* Same budget the firmware reserves: 288 pages * 40 B. */
#define HOST_PAGE_POOL_PAGES  288u
static uint8_t page_pool[HOST_PAGE_POOL_PAGES * CTF_PAGE_SIZE];

static ttf_reader_t   g_ttf;
static ctf_reader_t   g_ctf;
static ctf_resident_t g_res;

/* -------------------------------------------------------------------------- */
/* 1. Index geometry                                                          */
/* -------------------------------------------------------------------------- */

static void test_geometry(const char *ctf_path, const char *ttf_path)
{
    const ctf_header_t *h;

    printf("\n[1] open + header\n");

    check("ctf_open()", ctf_open(&g_ctf, ctf_path, ctf_cache,
                                 CTF_BLOCK_SIZE, CTF_BLOCK_COUNT,
                                 l1_shadow) == RT_OK);
    if (!ctf_is_open(&g_ctf))
    {
        return;
    }

    h = ctf_header(&g_ctf);
    printf("      upem=%u ascent=%d descent=%d gap=%d chars=%u entries=%u pages=%u\n",
           h->units_per_em, h->ascent, h->descent, h->line_gap,
           h->char_count, h->entry_count, h->page_index_count);

    check("units_per_em sane (256..16384)",
          (h->units_per_em >= 256u) && (h->units_per_em <= 16384u));
    check("ascent > 0 > descent", (h->ascent > 0) && (h->descent < 0));
    check("L1 shadow copied", g_ctf.l1_ready == 1u);

    /* Same call order the firmware uses: pin the index front right after the
     * open, before anything starts looking characters up. */
    check("ctf_load_resident()",
          ctf_load_resident(&g_ctf, page_pool, sizeof(page_pool),
                            &g_res) == RT_OK);

    check("ctf_verify_ttf() size matches",
          ctf_verify_ttf(&g_ctf, ttf_path, 0) == RT_OK);
    check("ctf_verify_ttf() rejects a wrong file",
          ctf_verify_ttf(&g_ctf, ctf_path, 0) == RT_FAIL);

    check("ttf_open()", ttf_open(&g_ttf, ttf_path, ttf_cache,
                                 TTF_BLOCK_SIZE, TTF_BLOCK_COUNT) == RT_OK);
    check("stb_adapter_open()", stb_adapter_open(&g_ttf, 0u) == RT_OK);
}

/* -------------------------------------------------------------------------- */
/* 2. Lookup: hits, empties, misses                                           */
/* -------------------------------------------------------------------------- */

typedef struct
{
    uint32_t cp;
    uint16_t glyph_id;
    uint16_t adv;
    int      empty;
} expect_t;

static void test_lookup(void)
{
    /* Values taken from `ttf2ctf.py --dump` on HarmonyOS_Sans_SC_Regular. */
    static const expect_t hits[] = {
        { 0x4E2D, 7341,  1000, 0 },   /* 中 */
        { 0x6587, 13319, 1000, 0 },   /* 文 */
        { 0x0041, 3,     664,  0 },   /* A  */
        { 0x0056, 169,   658,  0 },   /* V  */
        { 0x0030, 28405, 570,  0 },   /* 0  */
        { 0xFF0C, 28644, 1000, 0 },   /* ， */
        { 0x3002, 28764, 1000, 0 },   /* 。 */
        { 0x0020, 2,     270,  1 },   /* space: real glyph, no outline */
    };
    /* Nothing sane has these: private use, an unassigned CJK tail, astral. */
    static const uint32_t misses[] = {
        0xE000u, 0xE5B7u, 0x0B7Bu, 0x1F600u, 0x10FFFFu + 1u, 0xFFFFFFFFu
    };

    size_t i;

    printf("\n[2] lookup\n");
    if (!ctf_is_open(&g_ctf))
    {
        printf("  skipped - index not open\n");
        return;
    }

    /* The expected glyph ids and advances below were dumped from
     * HarmonyOS_Sans_SC_Regular.  Other fonts are still walked exhaustively in
     * [2b]; they just cannot be checked against these constants. */
    if (ctf_header(&g_ctf)->char_count != 29063u)
    {
        printf("  skipped - expected values are SC-specific "
               "(char_count=%u, reference=29063)\n",
               ctf_header(&g_ctf)->char_count);
        return;
    }

    for (i = 0u; i < (sizeof(hits) / sizeof(hits[0])); i++)
    {
        ctf_entry_t  e;
        ctf_result_t rc;
        char         msg[96];

        rc = ctf_find_unicode(&g_ctf, hits[i].cp, &e);

        (void)snprintf(msg, sizeof(msg),
                       "U+%04X found (rc=%d)", hits[i].cp, (int)rc);
        check(msg, rc == CTF_OK);
        if (rc != CTF_OK)
        {
            continue;
        }

        (void)snprintf(msg, sizeof(msg), "U+%04X glyph_id=%u",
                       hits[i].cp, e.glyph_id);
        check(msg, e.glyph_id == hits[i].glyph_id);

        (void)snprintf(msg, sizeof(msg), "U+%04X advance=%u",
                       hits[i].cp, e.advance_width);
        check(msg, e.advance_width == hits[i].adv);

        (void)snprintf(msg, sizeof(msg), "U+%04X empty=%d",
                       hits[i].cp, hits[i].empty);
        check(msg, (ctf_entry_is_empty(&e) != 0) == hits[i].empty);

        (void)snprintf(msg, sizeof(msg), "U+%04X glyf range inside TTF",
                       hits[i].cp);
        check(msg, (e.glyf_offset <= ttf_size(&g_ttf)) &&
                   (e.glyf_length <= (ttf_size(&g_ttf) - e.glyf_offset)));
    }

    for (i = 0u; i < (sizeof(misses) / sizeof(misses[0])); i++)
    {
        ctf_entry_t  e;
        ctf_result_t rc = ctf_find_unicode(&g_ctf, misses[i], &e);
        char         msg[96];

        (void)snprintf(msg, sizeof(msg), "U+%04X -> NOT_FOUND (rc=%d)",
                       misses[i], (int)rc);
        check(msg, rc == CTF_NOT_FOUND);
    }
}

/**
  * Walk every code point the addressing scheme can express and confirm the C
  * reader finds exactly as many characters as the header claims - no more, no
  * fewer - and that each one points at a real glyph inside the TTF.
  *
  * This is the test the Python --verify cannot do: it exercises the actual
  * L1 -> page -> bitmap -> popcount-rank path in the firmware source.
  */
static void test_full_walk(void)
{
    const ctf_header_t *h;
    uint32_t            cp;
    uint32_t            found = 0u;
    uint32_t            bad_glyph = 0u;
    uint32_t            bad_range = 0u;
    uint32_t            errors = 0u;
    uint32_t            ttf_sz;

    printf("\n[2b] full Unicode walk (0..0xFFFF)\n");
    if (!ctf_is_open(&g_ctf))
    {
        printf("  skipped - index not open\n");
        return;
    }

    h      = ctf_header(&g_ctf);
    ttf_sz = ttf_size(&g_ttf);

    for (cp = 0u; cp <= 0xFFFFu; cp++)
    {
        ctf_entry_t  e;
        ctf_result_t rc = ctf_find_unicode(&g_ctf, cp, &e);

        if (rc == CTF_NOT_FOUND)
        {
            continue;
        }
        if (rc != CTF_OK)
        {
            errors++;
            continue;
        }

        found++;

        if (e.glyph_id >= h->num_glyphs)
        {
            bad_glyph++;
        }
        if ((e.glyf_offset > ttf_sz) || (e.glyf_length > (ttf_sz - e.glyf_offset)))
        {
            bad_range++;
        }
    }

    printf("      found %u characters (header says %u)\n", found, h->char_count);

    check("walk finds exactly char_count characters", found == h->char_count);
    check("every glyph_id < num_glyphs", bad_glyph == 0u);
    check("every glyf range inside the TTF", bad_range == 0u);
    check("no lookup errors", errors == 0u);
}

/**
  * The resident index feature copies the page table into RAM so that lookups
  * (including the NOT_FOUND bit test) never touch the card.  This test proves
  * two things about that change:
  *
  *   1. Resident mode is actually used: page records come from RAM
  *      (page_ram_hits rise) and NOT from the card (page_sd_reads stay flat).
  *   2. Resident mode is observationally identical to the card path: every one
  *      of the 65536 code points yields the same result code, glyph id,
  *      advance and empty flag whether the page table is in RAM or on the SD.
  *
  * The second point is what stops a "faster path" from quietly becoming a
  * "different path".
  */
static int8_t   r_rc[65536];
static uint16_t r_gid[65536];
static uint16_t r_adv[65536];
static uint8_t  r_empty[65536];

static void test_resident_equivalence(void)
{
    uint32_t cp;
    uint32_t ram_before, sd_before, ram_after, sd_after;
    uint32_t mismatch = 0u;

    printf("\n[2c] resident page-table vs card-path equivalence\n");
    if (!ctf_is_open(&g_ctf))
    {
        printf("  skipped - index not open\n");
        return;
    }

    check("page table resident in RAM", g_res.page_resident == 1u);
    check("resident index reports non-zero RAM footprint",
          ctf_resident_bytes(&g_ctf) > 0u);

    /* --- Pass 1: resident mode.  Capture every result. ------------------- */
    ctf_page_stats(&g_ctf, &ram_before, &sd_before);
    for (cp = 0u; cp <= 0xFFFFu; cp++)
    {
        ctf_entry_t  e;
        ctf_result_t rc = ctf_find_unicode(&g_ctf, cp, &e);

        if (rc == CTF_ERR_IO || rc == CTF_ERR_RANGE)
        {
            r_rc[cp] = -1;
        }
        else
        {
            r_rc[cp] = (int8_t)rc;
        }
        if (rc == CTF_OK)
        {
            r_gid[cp]   = e.glyph_id;
            r_adv[cp]   = e.advance_width;
            r_empty[cp] = (ctf_entry_is_empty(&e) != 0u) ? 1u : 0u;
        }
        else
        {
            r_gid[cp]   = 0u;
            r_adv[cp]   = 0u;
            r_empty[cp] = 0u;
        }
    }
    ctf_page_stats(&g_ctf, &ram_after, &sd_after);

    check("resident pass reads pages from RAM (ram_hits up)",
          ram_after > ram_before);
    check("resident pass reads 0 pages from card (sd_reads flat)",
          sd_after == sd_before);

    /* --- Pass 2: degraded mode (page table back on the card). ------------ */
    g_ctf.page_all_ready = 0u;
    g_ctf.page_pool      = NULL;
    g_ctf.page_ready     = 0u;
    g_ctf.page_ram_hits  = 0u;
    g_ctf.page_sd_reads  = 0u;

    ctf_page_stats(&g_ctf, &ram_before, &sd_before);
    for (cp = 0u; cp <= 0xFFFFu; cp++)
    {
        ctf_entry_t  e;
        ctf_result_t rc = ctf_find_unicode(&g_ctf, cp, &e);
        int8_t      got;

        if (rc == CTF_ERR_IO || rc == CTF_ERR_RANGE)
        {
            got = -1;
        }
        else
        {
            got = (int8_t)rc;
        }
        if (got != r_rc[cp])
        {
            mismatch++;
            continue;
        }
        if ((rc == CTF_OK) &&
            ((r_gid[cp]   != e.glyph_id) ||
             (r_adv[cp]   != e.advance_width) ||
             (r_empty[cp] != ((ctf_entry_is_empty(&e) != 0u) ? 1u : 0u))))
        {
            mismatch++;
        }
    }
    ctf_page_stats(&g_ctf, &ram_after, &sd_after);

    check("degraded pass reads pages from card (sd_reads up)",
          sd_after > sd_before);
    check("resident and card paths agree on all 65536 code points",
          mismatch == 0u);

    /* Restore resident mode for the remaining tests. */
    g_ctf.page_all_ready = 1u;
    g_ctf.page_pool      = page_pool;
    g_ctf.page_ready     = 0u;
}

/* -------------------------------------------------------------------------- */
/* 3. Block cache: cross-block reads and out-of-range rejection               */
/* -------------------------------------------------------------------------- */

static void test_ttf_bounds(void)
{
    uint32_t size;
    uint8_t  buf[64];
    char     msg[96];

    printf("\n[3] ttf_reader bounds + cross-block\n");
    if (ttf_size(&g_ttf) == 0u)
    {
        printf("  skipped - ttf not open\n");
        return;
    }
    size = ttf_size(&g_ttf);

    (void)snprintf(msg, sizeof(msg), "read at 0");
    check(msg, ttf_read(&g_ttf, 0u, buf, 16u) == RT_OK);

    /* Straddle a 16 KB block boundary. */
    (void)snprintf(msg, sizeof(msg), "read across a %u B block edge",
                   TTF_BLOCK_SIZE);
    check(msg, ttf_read(&g_ttf, TTF_BLOCK_SIZE - 8u, buf, 32u) == RT_OK);

    /* The overflow case: offset + len wraps if computed in 32 bits. */
    (void)snprintf(msg, sizeof(msg), "reject offset near 0xFFFFFFFF");
    check(msg, ttf_read(&g_ttf, 0xFFFFFFF0u, buf, 32u) == RT_FAIL);

    (void)snprintf(msg, sizeof(msg), "reject len past EOF");
    check(msg, ttf_read(&g_ttf, size - 4u, buf, 32u) == RT_FAIL);

    (void)snprintf(msg, sizeof(msg), "accept the last byte");
    check(msg, ttf_read(&g_ttf, size - 1u, buf, 1u) == RT_OK);

    (void)snprintf(msg, sizeof(msg), "accept the last 64 B (partial block)");
    check(msg, ttf_read(&g_ttf, size - 64u, buf, 64u) == RT_OK);

    (void)snprintf(msg, sizeof(msg), "reject offset == size + 1");
    check(msg, ttf_read(&g_ttf, size + 1u, buf, 1u) == RT_FAIL);

    /* Sweep across the EOF boundary: every byte below it must be reachable
     * (including all of the final, short block) and none above it. */
    {
        uint32_t tail    = size - (size % TTF_BLOCK_SIZE);
        uint32_t tail_ok = 0u;
        uint32_t eof_ok  = 0u;
        uint32_t i;

        for (i = 0u; i < 64u; i++)
        {
            if (ttf_read(&g_ttf, tail + i, buf, 1u) == RT_OK)
            {
                tail_ok++;
            }
            if (ttf_read(&g_ttf, size + i, buf, 1u) == RT_OK)
            {
                eof_ok++;
            }
        }

        (void)snprintf(msg, sizeof(msg),
                       "last %u B of the final partial block readable",
                       64u);
        check(msg, tail_ok == 64u);

        (void)snprintf(msg, sizeof(msg), "nothing readable past EOF");
        check(msg, eof_ok == 0u);

        /* A read that starts inside the file but runs past EOF must fail. */
        (void)snprintf(msg, sizeof(msg), "reject a read straddling EOF");
        check(msg, ttf_read(&g_ttf, size - 16u, buf, 32u) == RT_FAIL);
    }

    /* Same block, read repeatedly, must not re-read from the card. */
    {
        uint32_t before, after, misses_a, misses_b;

        (void)before;
        ttf_stats(&g_ttf, &before, &misses_a, &after, &before);
        (void)ttf_read(&g_ttf, 1024u, buf, 32u);
        (void)ttf_read(&g_ttf, 1024u, buf, 32u);
        ttf_stats(&g_ttf, &before, &misses_b, &after, &before);
        (void)snprintf(msg, sizeof(msg), "repeat read served from cache "
                       "(misses %u -> %u)", misses_a, misses_b);
        check(msg, misses_b == misses_a);
    }
}

/* -------------------------------------------------------------------------- */
/* 4. Rasterise                                                               */
/* -------------------------------------------------------------------------- */

static void art(const uint8_t *bmp, uint16_t w, uint16_t h)
{
    static const char ramp[] = " .:-=+*#%@";
    uint16_t x, y;

    for (y = 0u; y < h; y++)
    {
        printf("      |");
        for (x = 0u; x < w; x++)
        {
            uint8_t v = bmp[((uint32_t)y * (uint32_t)w) + x];
            printf("%c", ramp[(v * 9u) / 255u]);
        }
        printf("|\n");
    }
}

static int render_one(uint32_t cp, uint16_t px, int show, int required)
{
    ctf_entry_t e;
    float       scale;
    int32_t     ix0, iy0, ix1, iy1;
    uint16_t    w, h;
    uint32_t    bytes;
    uint8_t    *buf;
    uint32_t    nonzero = 0u;
    uint32_t    i;

    if (ctf_find_unicode(&g_ctf, cp, &e) != CTF_OK)
    {
        /* A subset font legitimately lacks characters.  Only flag it when the
         * caller said the glyph must be there. */
        printf("  [%s] U+%04X not in index\n",
               required ? "FAIL" : "skip", cp);
        if (required)
        {
            g_fail++;
            return 0;
        }
        g_pass++;
        return 1;
    }

    scale = (float)px / (float)ctf_header(&g_ctf)->units_per_em;

    if (ctf_entry_is_empty(&e))
    {
        printf("  [PASS] U+%04X empty glyph, advance %d px at size %u\n",
               cp, (int)ctf_scale_advance((int32_t)e.advance_width, scale), px);
        g_pass++;
        return 1;
    }

    ctf_box_from_entry(&e, scale, &ix0, &iy0, &ix1, &iy1);
    w = (uint16_t)(ix1 - ix0 + 1);
    h = (uint16_t)(iy1 - iy0 + 1);
    bytes = (uint32_t)w * (uint32_t)h;

    buf = (uint8_t *)malloc(bytes);
    if (buf == NULL)
    {
        printf("  [FAIL] U+%04X out of host memory\n", cp);
        g_fail++;
        return 0;
    }

    if (stb_adapter_render(e.glyph_id, px, buf, w, h,
                           (int16_t)ix0, (int16_t)(-iy1)) != RT_OK)
    {
        printf("  [FAIL] U+%04X render failed\n", cp);
        free(buf);
        g_fail++;
        return 0;
    }

    for (i = 0u; i < bytes; i++)
    {
        if (buf[i] != 0u)
        {
            nonzero++;
        }
    }

    printf("  [%s] U+%04X @%upx  box %ux%u ofs(%d,%d) adv %d px  ink %u/%u px\n",
           (nonzero > 0u) ? "PASS" : "FAIL", cp, px, w, h,
           (int)ix0, (int)(-iy1),
           (int)ctf_scale_advance((int32_t)e.advance_width, scale),
           nonzero, bytes);
    if (nonzero > 0u)
    {
        g_pass++;
    }
    else
    {
        g_fail++;
    }

    if (show != 0)
    {
        art(buf, w, h);
    }

    free(buf);
    return 1;
}

static void test_render(void)
{
    printf("\n[4] rasterise\n");

    if (!stb_adapter_ready())
    {
        printf("  skipped - stb not ready\n");
        return;
    }

    /* 中 文 A 0 and space must be present in every font we ship; the rest are
     * best-effort so that a subset cut does not register as a failure. */
    (void)render_one(0x4E2Du, 24u, 1, 1);   /* 中 */
    (void)render_one(0x6587u, 24u, 1, 1);   /* 文 */
    (void)render_one(0x0041u, 16u, 0, 1);   /* A  */
    (void)render_one(0x0030u, 16u, 0, 1);   /* 0  */
    (void)render_one(0x0020u, 16u, 0, 1);   /* space: EMPTY, not an error */
    (void)render_one(0x00E9u, 32u, 0, 0);   /* e-acute: composite in many fonts */
    (void)render_one(0x91D1u, 32u, 0, 0);   /* 金: dense */
    (void)render_one(0x9F9Fu, 32u, 0, 0);   /* 鼟: 30 strokes, worst case */
}

/* -------------------------------------------------------------------------- */
/* 5. Stats                                                                   */
/* -------------------------------------------------------------------------- */

static void dump_stats(void)
{
    uint32_t hits, misses, fills, fill_bytes;
    uint32_t peak, fails;
    uint32_t cl, cn, ci;

    printf("\n[5] counters\n");

    ttf_stats(&g_ttf, &hits, &misses, &fills, &fill_bytes);
    printf("      ttf   hit=%u miss=%u f_read=%u bytes=%u\n",
           hits, misses, fills, fill_bytes);

    stb_adapter_arena_stats(&peak, &fails);
    printf("      arena peak=%u B fails=%u\n", peak, fails);

    ctf_stats(&g_ctf, &cl, &cn, &ci);
    printf("      ctf   lookups=%u not_found=%u io_errors=%u\n", cl, cn, ci);

    printf("      box mismatches = %u\n", stb_adapter_box_mismatches());

    check("no box mismatch between CTF and stb",
          stb_adapter_box_mismatches() == 0u);
    check("no arena overflow", fails == 0u);
    check("no CTF I/O errors", ci == 0u);
    check("block cache is doing its job (hits > misses)", hits > misses);
}

/* -------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        printf("usage: %s <font.ctf> <font.ttf>\n", argv[0]);
        return 2;
    }

    printf("== CTF host test ==\n");
    printf("   ctf %s\n   ttf %s\n", argv[1], argv[2]);

    test_geometry(argv[1], argv[2]);
    test_lookup();
    test_full_walk();
    test_resident_equivalence();
    test_ttf_bounds();
    test_render();
    dump_stats();

    printf("\n== %d passed, %d failed ==\n", g_pass, g_fail);

    ctf_close(&g_ctf);
    ttf_close(&g_ttf);

    return (g_fail == 0) ? 0 : 1;
}
