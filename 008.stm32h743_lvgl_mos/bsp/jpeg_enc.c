#include "jpeg_enc.h"
#include <string.h>

#ifndef JPEG_ENC_NO_STDIO
#include <stdio.h>
#define DBG(...) fprintf(stderr, __VA_ARGS__)
#else
#define DBG(...)
#endif

/* ------------------------------------------------------------------ */
/* zig-zag permutation (natural -> zig-zag index)                     */
/* ------------------------------------------------------------------ */
static const int s_zigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

/* ------------------------------------------------------------------ */
/* standard JPEG Huffman tables (Annex K)                             */
/* ------------------------------------------------------------------ */
static const uint8_t s_dc_l_bits[17] = {0,0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
static const uint8_t s_dc_l_vals[12] = {0,1,2,3,4,5,6,7,8,9,10,11};

static const uint8_t s_dc_c_bits[17] = {0,0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0};
static const uint8_t s_dc_c_vals[12] = {0,1,2,3,4,5,6,7,8,9,10,11};

static const uint8_t s_ac_l_bits[17] = {0,0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d};
static const uint8_t s_ac_l_vals[162] = {
    0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,
    0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,
    0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
    0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,
    0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,
    0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
    0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,
    0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,
    0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
    0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
    0xf9,0xfa
};
/* chrominance AC table == luminance AC table in Annex K */
#define s_ac_c_bits  s_ac_l_bits
#define s_ac_c_vals  s_ac_l_vals

/* ------------------------------------------------------------------ */
/* standard quantization tables (zig-zag order), quality is scaled    */
/* ------------------------------------------------------------------ */
static const uint8_t s_q_luma[64] = {
    16, 11, 12, 14, 12, 10, 16, 14, 13, 14, 18, 17, 16, 19, 24, 40,
    26, 24, 22, 22, 24, 49, 35, 37, 29, 40, 58, 51, 61, 60, 57, 51,
    56, 55, 64, 72, 92, 78, 64, 68, 87, 69, 55, 56, 80,109, 81, 87,
    95, 98,103,104,103, 62, 77,113,121,112,100,120, 92,101,103, 99
};
static const uint8_t s_q_chroma[64] = {
    17, 18, 18, 24, 21, 24, 47, 26, 26, 47, 99, 66, 56, 66, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99
};

#define JPEG_QUALITY 72

/* ------------------------------------------------------------------ */
/* bit writer (MSB-first), with 0xFF stuffing for entropy-coded data  */
/* ------------------------------------------------------------------ */
typedef struct {
    uint8_t *p;
    int      n;      /* bytes written                                   */
    int      bitbuf; /* pending bits (valid low bitcnt bits)           */
    int      bitcnt; /* number of valid pending bits (0..7)           */
    int      stuff;  /* non-zero => stuff 0x00 after a 0xFF byte      */
} bw_t;

static void bw_byte(bw_t *b, int byte)
{
    b->p[b->n++] = (uint8_t)byte;
    if (b->stuff && (byte == 0xFF)) b->p[b->n++] = 0x00;
}

static void bw_put(bw_t *b, unsigned code, int len)
{
    while (len > 0) {
        int take = 8 - b->bitcnt;
        if (take > len) take = len;
        int val = (code >> (len - take)) & ((1 << take) - 1);
        b->bitbuf = (b->bitbuf << take) | val;
        b->bitcnt += take;
        if (b->bitcnt == 8) {
            uint8_t byte = (uint8_t)b->bitbuf;
            b->p[b->n++] = byte;
            if (b->stuff && (byte == 0xFF)) b->p[b->n++] = 0x00;
            b->bitbuf = 0;
            b->bitcnt = 0;
        }
        len -= take;
    }
}

static void bw_flush(bw_t *b)
{
    if (b->bitcnt > 0) {
        b->bitbuf <<= (8 - b->bitcnt);
        uint8_t byte = (uint8_t)b->bitbuf;
        b->p[b->n++] = byte;
        if (b->stuff && (byte == 0xFF)) b->p[b->n++] = 0x00;
        b->bitcnt = 0;
        b->bitbuf = 0;
    }
}

/* ------------------------------------------------------------------ */
/* canonical Huffman code tables (built once from the standard tables)*/
/*   code[256] / len[256] : symbol -> (code, length)                  */
/* ------------------------------------------------------------------ */
static uint16_t s_hcode[4][256];
static uint8_t  s_hlen[4][256];

static void build_huff(const uint8_t *bits, const uint8_t *vals, int nvals,
                       uint16_t *code, uint8_t *len)
{
    int k = 0, c = 0;
    for (int l = 1; l <= 16; l++) {
        for (int i = 0; i < bits[l]; i++) {
            /* canonical Huffman code; emitted MSB-first by bw_put() which
             * shifts the high bits out first, matching baseline decoders. */
            code[vals[k]] = (uint16_t)c;
            len[vals[k]]   = (uint8_t)l;
            k++;
            c++;
        }
        c <<= 1;
    }
    (void)nvals;
}

static void build_all_tables(void)
{
    static int done = 0;
    if (done) return;
    memset(s_hcode, 0, sizeof(s_hcode));
    memset(s_hlen,  0, sizeof(s_hlen));
    build_huff(s_dc_l_bits, s_dc_l_vals, 12, s_hcode[0], s_hlen[0]);
    build_huff(s_ac_l_bits, s_ac_l_vals, 162, s_hcode[1], s_hlen[1]);
    build_huff(s_dc_c_bits, s_dc_c_vals, 12, s_hcode[2], s_hlen[2]);
    build_huff(s_ac_c_bits, s_ac_c_vals, 162, s_hcode[3], s_hlen[3]);
    done = 1;
}

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */
static void rgb565_to_ycbcr(uint16_t p, double *Y, double *Cb, double *Cr)
{
    int r = (p >> 11) & 0x1F;
    int g = (p >>  5) & 0x3F;
    int b =  p        & 0x1F;
    /* expand 5/6-bit RGB565 channels to 8-bit (val * 255 / maxval) */
    r = (r * 2114 + 128) >> 8;   /* 5-bit -> 8-bit  (2114/256 = 8.258) */
    g = (g * 1055 +  64) >> 8;   /* 6-bit -> 8-bit  (1055/256 = 4.121) */
    b = (b * 2114 + 128) >> 8;   /* 5-bit -> 8-bit */
    *Y  =  0.299   * r + 0.587   * g + 0.114   * b;
    *Cb = -0.168736* r - 0.331264* g + 0.5     * b + 128.0;
    *Cr =  0.5     * r - 0.418688* g - 0.081312* b + 128.0;
}

static int category(int val)
{
    int a = val < 0 ? -val : val;
    int c = 0;
    while (a > 0) { a >>= 1; c++; }
    return c;
}

/* Precomputed 8x8 DCT-II basis: basis[u][x] = Cu * cos((2x+1) * u * pi / 16),
 * with Cu = 1/sqrt(2) for u == 0 else 1.  Building it once and applying it as
 * two separable 1D passes keeps the per-block cost at 1024 multiplies instead
 * of 4096 (and removes every cos() call from the hot path), so a 240x240
 * capture finishes in well under a second even on the Cortex-M7's soft-float
 * double unit.  The values match the canonical DCT exactly. */
static const double s_dct_basis[8][8] = {
  { 0.7071067812, 0.7071067812, 0.7071067812, 0.7071067812, 0.7071067812, 0.7071067812, 0.7071067812, 0.7071067812 },
  { 0.9807852804, 0.8314696123, 0.5555702330, 0.1950903220, -0.1950903220, -0.5555702330, -0.8314696123, -0.9807852804 },
  { 0.9238795325, 0.3826834324, -0.3826834324, -0.9238795325, -0.9238795325, -0.3826834324, 0.3826834324, 0.9238795325 },
  { 0.8314696123, -0.1950903220, -0.9807852804, -0.5555702330, 0.5555702330, 0.9807852804, 0.1950903220, -0.8314696123 },
  { 0.7071067812, -0.7071067812, -0.7071067812, 0.7071067812, 0.7071067812, -0.7071067812, -0.7071067812, 0.7071067812 },
  { 0.5555702330, -0.9807852804, 0.1950903220, 0.8314696123, -0.8314696123, -0.1950903220, 0.9807852804, -0.5555702330 },
  { 0.3826834324, -0.9238795325, 0.9238795325, -0.3826834324, -0.3826834324, 0.9238795325, -0.9238795325, 0.3826834324 },
  { 0.1950903220, -0.5555702330, 0.8314696123, -0.9807852804, 0.9807852804, -0.8314696123, 0.5555702330, -0.1950903220 },
};

/* round-half-away-from-zero to int16, without pulling in libm's floor() */
static int16_t dct_round(double v)
{
    double  r = (v >= 0.0) ? (v + 0.5) : (v - 0.5);
    int     i = (int)r;
    /* (int) truncates toward zero; emulate floor() for negative non-integers */
    if ((r < 0.0) && ((double)i > r))
    {
        i -= 1;
    }
    if (i >  32767) i =  32767;
    if (i < -32768) i = -32768;
    return (int16_t)i;
}

/* forward DCT (separable 1D x 2) -> 64 int16 coefficients (natural order) */
static void fdct(const double *in, int16_t *out)
{
    double tmp[64];

    /* 1D DCT on rows */
    for (int y = 0; y < 8; y++) {
        const double *row = in + y * 8;
        for (int u = 0; u < 8; u++) {
            double sum = 0.0;
            const double *b = s_dct_basis[u];
            for (int x = 0; x < 8; x++) sum += row[x] * b[x];
            tmp[y * 8 + u] = sum;
        }
    }

    /* 1D DCT on columns, with the 1/4 JPEG scaling folded into the final pass */
    for (int x = 0; x < 8; x++) {
        for (int v = 0; v < 8; v++) {
            double sum = 0.0;
            const double *b = s_dct_basis[v];
            for (int y = 0; y < 8; y++) sum += tmp[y * 8 + x] * b[y];
            out[v * 8 + x] = dct_round(sum * 0.25);
        }
    }
}

/* ------------------------------------------------------------------ */
/* public entry                                                       */
/* ------------------------------------------------------------------ */
int jpeg_encode_rgb565(const uint16_t *rgb565, int w, int h,
                       uint8_t *out, int out_capacity)
{
    if (!rgb565 || !out || w <= 0 || h <= 0 || out_capacity <= 0) return -1;

    build_all_tables();

    /* scaled quant tables (zig-zag order) */
    uint8_t qt[2][64];
    {
        int scale = (JPEG_QUALITY < 50) ? (5000 / JPEG_QUALITY)
                                        : (200 - 2 * JPEG_QUALITY);
        for (int i = 0; i < 64; i++) {
            int ql = (s_q_luma[i]   * scale + 50) / 100; if (ql < 1) ql = 1; if (ql > 255) ql = 255;
            int qc = (s_q_chroma[i] * scale + 50) / 100; if (qc < 1) qc = 1; if (qc > 255) qc = 255;
            qt[0][i] = (uint8_t)ql;
            qt[1][i] = (uint8_t)qc;
        }
    }

    bw_t b;
    b.p = out; b.n = 0; b.bitbuf = 0; b.bitcnt = 0; b.stuff = 0;

    /* SOI */
    bw_byte(&b, 0xFF); bw_byte(&b, 0xD8);
    /* APP0 JFIF: length 16 (14 data bytes): "JFIF\0" + ver + units +
     * Xdensity + Ydensity + Xthumbnail + Ythumbnail */
    bw_byte(&b, 0xFF); bw_byte(&b, 0xE0);
    bw_byte(&b, 0x00); bw_byte(&b, 0x10);
    bw_byte(&b, 'J'); bw_byte(&b, 'F'); bw_byte(&b, 'I'); bw_byte(&b, 'F');
    bw_byte(&b, 0x00);
    bw_byte(&b, 0x01); bw_byte(&b, 0x01);   /* version 1.1 */
    bw_byte(&b, 0x00);                       /* units: 0 = aspect ratio   */
    bw_byte(&b, 0x00); bw_byte(&b, 0x01);    /* Xdensity = 1             */
    bw_byte(&b, 0x00); bw_byte(&b, 0x01);    /* Ydensity = 1             */
    bw_byte(&b, 0x00); bw_byte(&b, 0x00);    /* X/Y thumbnail = 0 (none) */

    /* DQT x2 */
    for (int t = 0; t < 2; t++) {
        bw_byte(&b, 0xFF); bw_byte(&b, 0xDB);
        bw_byte(&b, 0x00); bw_byte(&b, 0x43);   /* 2 + 64 + 1 */
        bw_byte(&b, (uint8_t)t);                /* precision 0, id t */
        for (int k = 0; k < 64; k++) bw_byte(&b, qt[t][k]);
    }

    /* SOF0 */
    bw_byte(&b, 0xFF); bw_byte(&b, 0xC0);
    bw_byte(&b, 0x00); bw_byte(&b, 0x11);       /* 17 bytes */
    bw_byte(&b, 0x08);                          /* precision 8 */
    bw_byte(&b, (uint8_t)(h >> 8)); bw_byte(&b, (uint8_t)h);
    bw_byte(&b, (uint8_t)(w >> 8)); bw_byte(&b, (uint8_t)w);
    bw_byte(&b, 0x03);                          /* 3 components */
    bw_byte(&b, 0x01); bw_byte(&b, 0x11); bw_byte(&b, 0x00); /* Y  : H1V1, Tq0 */
    bw_byte(&b, 0x02); bw_byte(&b, 0x11); bw_byte(&b, 0x01); /* Cb : H1V1, Tq1 */
    bw_byte(&b, 0x03); bw_byte(&b, 0x11); bw_byte(&b, 0x01); /* Cr : H1V1, Tq1 */

    /* DHT x4 */
    {
        const struct { const uint8_t *bits; const uint8_t *vals; int n; int tc; int th; } t[4] = {
            { s_dc_l_bits, s_dc_l_vals, 12,  0, 0 },
            { s_ac_l_bits, s_ac_l_vals, 162, 1, 0 },
            { s_dc_c_bits, s_dc_c_vals, 12,  0, 1 },
            { s_ac_c_bits, s_ac_c_vals, 162, 1, 1 },
        };
        for (int i = 0; i < 4; i++) {
            int total = 0;
            for (int l = 1; l <= 16; l++) total += t[i].bits[l];
            bw_byte(&b, 0xFF); bw_byte(&b, 0xC4);
            bw_byte(&b, (uint8_t)((2 + 1 + 16 + total) >> 8));
            bw_byte(&b, (uint8_t)( 2 + 1 + 16 + total));
            bw_byte(&b, (uint8_t)((t[i].tc << 4) | t[i].th));
            for (int l = 1; l <= 16; l++) bw_byte(&b, t[i].bits[l]);
            for (int k = 0; k < total; k++)   bw_byte(&b, t[i].vals[k]);
        }
    }

    /* SOS */
    bw_byte(&b, 0xFF); bw_byte(&b, 0xDA);
    bw_byte(&b, 0x00); bw_byte(&b, 0x0C);       /* 12 bytes */
    bw_byte(&b, 0x03);                          /* 3 components */
    bw_byte(&b, 0x01); bw_byte(&b, 0x00);       /* Y  -> DC0 AC0 */
    bw_byte(&b, 0x02); bw_byte(&b, 0x11);       /* Cb -> DC1 AC1 */
    bw_byte(&b, 0x03); bw_byte(&b, 0x11);       /* Cr -> DC1 AC1 */
    bw_byte(&b, 0x00); bw_byte(&b, 0x3F); bw_byte(&b, 0x00); /* Ss Se AhAl */

    /* ---- entropy-coded scan data ---- */
    b.stuff = 1;

    int mcux = (w + 7) / 8;
    int mcuy = (h + 7) / 8;
    int prev_dc[3] = {0, 0, 0};
    int s_nonzero_ac = 0;
    int s_dc_abs = 0;

    for (int my = 0; my < mcuy; my++) {
        for (int mx = 0; mx < mcux; mx++) {
            int bx = mx * 8;
            int by = my * 8;
            /* 3 components per MCU (4:4:4, 1x1 sampling) */
            for (int ci = 0; ci < 3; ci++) {
                double  blk[64];
                int16_t coeff[64];
                /* gather 8x8, pad edges */
                for (int yy = 0; yy < 8; yy++) {
                    int sy = by + yy; if (sy >= h) sy = h - 1;
                    for (int xx = 0; xx < 8; xx++) {
                        int sx = bx + xx; if (sx >= w) sx = w - 1;
                        uint16_t p = rgb565[sy * w + sx];
                        double Y, Cb, Cr;
                        rgb565_to_ycbcr(p, &Y, &Cb, &Cr);
                        double s = (ci == 0) ? Y : (ci == 1) ? Cb : Cr;
                        blk[yy * 8 + xx] = s - 128.0;   /* level shift */
                    }
                }
                fdct(blk, coeff);

                /* quantize (zig-zag)
                 * Clamp to the range the Huffman tables can represent:
                 *   DC difference : 11-bit signed  -> [-2047, 2047]
                 *   AC magnitude  : 10-bit signed  -> [-1023, 1023]
                 * (A naive +/-128 clamp destroys real images: a dark 8x8
                 *  block's DC alone is ~ -900 after quantization.) */
                int zz[64];
                for (int k = 0; k < 64; k++) {
                    int nat = s_zigzag[k];
                    int q = qt[ci == 0 ? 0 : 1][k];
                    int v = coeff[nat];
                    int lvl = (v + (v >= 0 ? q / 2 : -q / 2)) / q;
                    int lim = (k == 0) ? 2047 : 1023;
                    if (lvl >  lim) lvl =  lim;
                    if (lvl < -lim) lvl = -lim;
                    zz[k] = lvl;
                }

                /* DC */
                int diff = zz[0] - prev_dc[ci];
                prev_dc[ci] = zz[0];
                s_dc_abs += (diff < 0 ? -diff : diff);
                int dc_tbl = (ci == 0) ? 0 : 2;   /* 0=DC_L, 2=DC_C */
                int ac_tbl = (ci == 0) ? 1 : 3;   /* 1=AC_L, 3=AC_C */
                int cs = category(diff);
                bw_put(&b, s_hcode[dc_tbl][cs], s_hlen[dc_tbl][cs]);
                if (cs > 0) {
                    unsigned mag = (diff < 0) ? (unsigned)(diff + (1 << cs) - 1) : (unsigned)diff;
                    bw_put(&b, mag, cs);
                }

                /* AC */
                int r = 0;
                for (int k = 1; k < 64; k++) {
                    int ac = zz[k];
                    if (ac == 0) {
                        r++;
                        if (r == 16) {
                            bw_put(&b, s_hcode[ac_tbl][0xF0], s_hlen[ac_tbl][0xF0]); /* ZRL */
                            r = 0;
                        }
                        continue;
                    }
                    s_nonzero_ac++;
                    while (r > 15) {
                        bw_put(&b, s_hcode[ac_tbl][0xF0], s_hlen[ac_tbl][0xF0]);
                        r -= 16;
                    }
                    int sz = category(ac);
                    int sym = (r << 4) | sz;
                    bw_put(&b, s_hcode[ac_tbl][sym], s_hlen[ac_tbl][sym]);
                    unsigned mag = (ac < 0) ? (unsigned)(ac + (1 << sz) - 1) : (unsigned)ac;
                    bw_put(&b, mag, sz);
                    r = 0;
                }
                if (r > 0) {
                    bw_put(&b, s_hcode[ac_tbl][0x00], s_hlen[ac_tbl][0x00]); /* EOB */
                }
            }
        }
    }

    DBG("[jpeg] nonzero AC total=%d, dc_sum(abs)=%d, mcu=%dx%d\n",
        s_nonzero_ac, s_dc_abs, mcux, mcuy);

    bw_flush(&b);

    /* EOI (marker must NOT be stuffed) */
    b.stuff = 0;
    bw_byte(&b, 0xFF); bw_byte(&b, 0xD9);

    if (b.n > out_capacity) return -1;
    return b.n;
}
