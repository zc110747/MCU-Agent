/**
 * Host-side regression test for the image-viewer "涂抹" (garbled display) bug.
 *
 * Root cause: TJpgDec's JDEC.swap field controls whether each RGB565 pixel is
 * byte-swapped on output.  jd_prepare() SAVES and RESTORES jd->swap, so the
 * field keeps whatever garbage was on the stack when JDEC was declared.  If
 * non-zero, every decoded pixel has its high/low bytes swapped -> garbled
 * colours on the ST7789, which (via SPI 16-bit mode) expects standard,
 * non-swapped RGB565 -- the SAME layout the BMP path (rgb565()) and the NES
 * master palette (nes_palette_rgb565[]) use.
 *
 * This test compiles the EXACT RGB565 conversion block from third_party/tjpgd/
 * tjpgd.c (the swap / non-swap branches) and feeds it known RGB888 inputs,
 * proving:
 *   swap = 0  -> standard RGB565 (matches NES palette / BMP path)
 *   swap = 1  -> byte-swapped RGB565 (the garbled "涂抹" symptom)
 *
 * Build & run (host, x86):
 *   cc -DJD_FORMAT=1 tests/test_jpg_rgb565.c -o /tmp/t && /tmp/t
 * Expect exit 0 and "ALL CHECKS PASSED".
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#ifndef JD_FORMAT
#define JD_FORMAT 1
#endif

/* ---- verbatim from third_party/tjpgd/tjpgd.c (outfunc, RGB565 block) ------ */
static void convert_rgb888_to_rgb565(uint8_t *workbuf, unsigned int n, int swap)
{
    if (JD_FORMAT == 1) {
        uint8_t *s = (uint8_t*)workbuf;
        uint16_t w, *d = (uint16_t*)s;

        if (swap)
        {
          do {
            w =  (*s++ & 0xF8) << 8;    // RRRRR-----------
            w |= (*s++ & 0xFC) << 3;    // -----GGGGGG-----
            w |= *s++ >> 3;             // -----------BBBBB
            *d++ = (w << 8) | (w >> 8); // Swap bytes
          }   while (--n);
        }
        else
        {
          do {
            w = ( *s++ & 0xF8) << 8;  // RRRRR-----------
            w |= (*s++ & 0xFC) << 3;  // -----GGGGGG-----
            w |= *s++ >> 3;           // -----------BBBBB
            *d++ = w;
          }   while (--n);
        }
    }
}
/* -------------------------------------------------------------------------- */

/* Mirror of app/img_decode.c rgb565() -- the BMP-path conversion. */
static uint16_t bmp_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xF8U) << 8) |
                      ((uint16_t)(g & 0xFCU) << 3) |
                      ((uint16_t)b >> 3));
}

typedef struct { uint8_t r, g, b; const char *name; } sample_t;
static const sample_t SAMPLES[] = {
    {0xFF, 0x00, 0x00, "red"},
    {0x00, 0xFF, 0x00, "green"},
    {0x00, 0x00, 0xFF, "blue"},
    {0xFF, 0xFF, 0xFF, "white"},
    {0x00, 0x00, 0x00, "black"},
    {0x80, 0x80, 0x80, "gray"},
};

int main(void)
{
    int fail = 0;
    size_t ns = sizeof(SAMPLES) / sizeof(SAMPLES[0]);

    for (size_t i = 0; i < ns; i++)
    {
        const sample_t *s = &SAMPLES[i];

        /* Build an RGB888 triple. */
        uint8_t buf[3];
        buf[0] = s->r; buf[1] = s->g; buf[2] = s->b;

        uint8_t buf_swap[3];
        memcpy(buf_swap, buf, 3);

        /* Fixed code path: swap = 0. */
        convert_rgb888_to_rgb565(buf, 1, 0);
        uint16_t fixed = *(uint16_t *)buf;

        /* Buggy code path: swap = 1. */
        convert_rgb888_to_rgb565(buf_swap, 1, 1);
        uint16_t buggy = *(uint16_t *)buf_swap;

        /* Reference: what BMP path / NES palette produce (standard RGB565). */
        uint16_t ref = bmp_rgb565(s->r, s->g, s->b);

        /* Clean 16-bit byte swap of the fixed value (proper masking; the C
         * shift in TJpgDec's `(w<<8)|(w>>8)` is computed in int then stored
         * back into a uint16_t, which truncates to exactly this). */
        uint16_t swapped = (uint16_t)(((fixed & 0xFFU) << 8) | (fixed >> 8));
        int is_symmetric = (ref == swapped); /* white/black swap to themselves */

        int ok_fixed = (fixed == ref);
        int ok_buggy = is_symmetric ? (buggy == ref) : (buggy != ref);
        int buggy_is_swapped = (buggy == swapped);

        printf("[%s] ref=0x%04X fixed(swap0)=0x%04X buggy(swap1)=0x%04X%s"
               "  fixed_ok=%d buggy_ok=%d buggy_swapped=%d\n",
               s->name, ref, fixed, buggy, is_symmetric ? " (symmetric)" : "",
               ok_fixed, ok_buggy, buggy_is_swapped);

        if (!ok_fixed) { printf("  !! FIXED path does NOT match reference\n"); fail++; }
        if (!ok_buggy) { printf("  !! buggy path value unexpected\n"); fail++; }
        if (!buggy_is_swapped) { printf("  !! buggy path not a clean byte swap\n"); fail++; }
    }

    printf("\n%s\n", fail == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED");
    return fail == 0 ? 0 : 1;
}
