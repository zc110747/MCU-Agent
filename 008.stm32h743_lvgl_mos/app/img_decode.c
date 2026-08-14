/**
  ******************************************************************************
  * @file    img_decode.c
  * @brief   BMP / JPEG decode into one 240x240 RGB565 frame (see img_decode.h).
  ******************************************************************************
  */
#include "img_decode.h"
#include "drv_spi_oled.h"
#include "sram_pool.h"
#include "ff.h"
#include "tjpgd.h"
#include <stdio.h>
#include <string.h>

/* The frame lives in .bss (AXI-SRAM, RAM_D1): 240 * 240 * 2 = 112.5 kB.  It is
 * deliberately *not* in DTCM - that block is reserved for the NES machine
 * state, and LCD_CopyBuffer() streams out of this one anyway. */
static uint16_t s_fb[IMG_W * IMG_H];

/* Rows pushed per SPI transaction (240 * 30 * 2 = 14.4 kB), same trade-off the
 * NES blitter makes: few enough transactions to hide the per-call SPI
 * re-configuration, small enough not to need a second frame's worth of RAM. */
#define IMG_BAND_LINES  30

/* One source scanline of a BMP.  8 kB caps the input at 2730 px wide (24 bpp)
 * or 2048 px (32 bpp), which is far beyond anything worth showing here. */
#define BMP_LINE_MAX    8192
static uint8_t  s_line[BMP_LINE_MAX];

/* TJpgDec work pool.  tjpgdcnf.h sets JD_FASTDECODE=2, whose worst case is
 * 9644 bytes (3500 + 6144 for the two fast huffman LUTs); our own `cap`
 * captures are 4:4:4 / 3-component JPEGs that exercise the full 6 kB LUT, so
 * the old fixed 8 kB static buffer was too small and every such file failed
 * with JDR_MEM1/JDR_MEM2 ("内存不足").  The pool is now taken from the shared
 * SRAM arena (RAM_D2, ~286 kB free whenever the image viewer is the active
 * page - NES is a different, mutually exclusive page) at decode time and
 * handed back on exit.  32 kB gives 3x headroom over the documented worst
 * case, even for pathological multi-table JPEGs. */
#define IMG_JPEG_POOL   (32u * 1024u)

/*----------------------------------------------------------------------------
 *  Helpers
 *--------------------------------------------------------------------------*/

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xF8U) << 8) |
                      ((uint16_t)(g & 0xFCU) << 3) |
                      ((uint16_t)b >> 3));
}

static void frame_clear(void)
{
    (void)memset(s_fb, 0, sizeof(s_fb));
}

/** Fit src into the panel: never upscale, keep the aspect ratio. */
static void fit_box(int sw, int sh, int *ow, int *oh)
{
    if ((sw <= IMG_W) && (sh <= IMG_H))
    {
        *ow = sw;
        *oh = sh;
        return;
    }

    if ((sw * IMG_H) >= (sh * IMG_W))
    {
        *ow = IMG_W;
        *oh = (sh * IMG_W) / sw;
    }
    else
    {
        *oh = IMG_H;
        *ow = (sw * IMG_H) / sh;
    }

    if (*ow < 1) { *ow = 1; }
    if (*oh < 1) { *oh = 1; }
}

/*----------------------------------------------------------------------------
 *  BMP
 *--------------------------------------------------------------------------*/

static int decode_bmp(FIL *fp, img_info_t *info, const char **reason)
{
    uint8_t  hdr[54];
    UINT     br = 0U;
    uint32_t off_bits;
    uint32_t dib;
    int32_t  w;
    int32_t  h;
    uint16_t bpp;
    uint32_t comp;
    uint32_t stride;
    int      bottom_up;
    int      src_h;
    int      bytes_pp;
    int      out_w;
    int      out_h;
    int      out_x;
    int      out_y;
    int      oy;

    if (f_lseek(fp, 0U) != FR_OK)
    {
        *reason = "读取失败";
        return -1;
    }
    if ((f_read(fp, hdr, sizeof(hdr), &br) != FR_OK) || (br != sizeof(hdr)))
    {
        *reason = "文件不完整";
        return -1;
    }

    off_bits = rd32(&hdr[10]);
    dib      = rd32(&hdr[14]);
    w        = (int32_t)rd32(&hdr[18]);
    h        = (int32_t)rd32(&hdr[22]);
    bpp      = rd16(&hdr[28]);
    comp     = rd32(&hdr[30]);

    if (dib < 40U)
    {
        *reason = "BMP 版本太老";
        return -1;
    }
    if ((bpp != 24U) && (bpp != 32U))
    {
        *reason = "只支持 24/32 位 BMP";
        return -1;
    }
    if ((comp != 0U) && (comp != 3U))
    {
        *reason = "BMP 已压缩";
        return -1;
    }

    bottom_up = (h > 0) ? 1 : 0;
    src_h     = (h > 0) ? (int)h : (int)(-h);
    bytes_pp  = (int)(bpp / 8U);

    if ((w <= 0) || (src_h <= 0))
    {
        *reason = "尺寸无效";
        return -1;
    }

    stride = (((uint32_t)w * (uint32_t)bytes_pp) + 3U) & ~3U;
    if (stride > (uint32_t)BMP_LINE_MAX)
    {
        *reason = "图片太宽";
        return -1;
    }

    fit_box((int)w, src_h, &out_w, &out_h);
    out_x = (IMG_W - out_w) / 2;
    out_y = (IMG_H - out_h) / 2;

    frame_clear();

    for (oy = 0; oy < out_h; oy++)
    {
        int       sy  = (oy * src_h) / out_h;
        int       row = (bottom_up != 0) ? (src_h - 1 - sy) : sy;
        uint16_t *dst = &s_fb[((out_y + oy) * IMG_W) + out_x];
        int       ox;

        if (f_lseek(fp, (FSIZE_t)off_bits + ((FSIZE_t)row * stride)) != FR_OK)
        {
            *reason = "读取失败";
            return -1;
        }
        if ((f_read(fp, s_line, (UINT)stride, &br) != FR_OK) ||
            (br != (UINT)stride))
        {
            *reason = "文件不完整";
            return -1;
        }

        for (ox = 0; ox < out_w; ox++)
        {
            const uint8_t *p = &s_line[(size_t)((ox * (int)w) / out_w) *
                                       (size_t)bytes_pp];
            /* BMP stores BGR(A). */
            dst[ox] = rgb565(p[2], p[1], p[0]);
        }
    }

    info->src_w     = (int)w;
    info->src_h     = src_h;
    info->out_w     = out_w;
    info->out_h     = out_h;
    info->scale_num = (out_w * 100) / (int)w;
    info->format    = "BMP";

    return 0;
}

/*----------------------------------------------------------------------------
 *  JPEG (TJpgDec)
 *--------------------------------------------------------------------------*/

typedef struct
{
    FIL *fp;
    int  ox;            /* where decoded pixel (0,0) lands in the frame */
    int  oy;
} jpg_ctx_t;

static size_t jpg_in(JDEC *jd, uint8_t *buf, size_t n)
{
    jpg_ctx_t *c  = (jpg_ctx_t *)jd->device;
    UINT       br = 0U;

    if (buf != NULL)
    {
        if (f_read(c->fp, buf, (UINT)n, &br) != FR_OK)
        {
            return 0U;
        }
        return (size_t)br;
    }

    /* buf == NULL means "skip n bytes". */
    if (f_lseek(c->fp, f_tell(c->fp) + (FSIZE_t)n) != FR_OK)
    {
        return 0U;
    }
    return n;
}

static int jpg_out(JDEC *jd, void *bitmap, JRECT *rect)
{
    const jpg_ctx_t *c   = (const jpg_ctx_t *)jd->device;
    const uint16_t  *src = (const uint16_t *)bitmap;
    int              rw  = (int)rect->right  - (int)rect->left + 1;
    int              rh  = (int)rect->bottom - (int)rect->top  + 1;
    int              r;

    for (r = 0; r < rh; r++)
    {
        int y = c->oy + (int)rect->top + r;
        int cidx;

        if ((y < 0) || (y >= IMG_H))
        {
            continue;
        }

        for (cidx = 0; cidx < rw; cidx++)
        {
            int x = c->ox + (int)rect->left + cidx;

            if ((x < 0) || (x >= IMG_W))
            {
                continue;
            }
            s_fb[(y * IMG_W) + x] = src[(r * rw) + cidx];
        }
    }

    return 1;                                   /* continue */
}

static const char *jpg_reason(JRESULT res)
{
    switch (res)
    {
    case JDR_INP:   return "读取失败";
    case JDR_MEM1:
    case JDR_MEM2:  return "内存不足";
    case JDR_PAR:   return "参数错误";
    case JDR_FMT1:  return "JPEG 数据损坏";
    case JDR_FMT2:  return "JPEG 格式不支持";
    case JDR_FMT3:  return "非基线 JPEG";
    default:        return "解码失败";
    }
}

static int decode_jpeg(FIL *fp, img_info_t *info, const char **reason)
{
    JDEC         jd;
    jpg_ctx_t    ctx;
    JRESULT      res;
    sram_region_t region = SRAM_REGION_D2;
    void        *pool   = NULL;
    uint8_t      scale  = 0U;
    int          out_w;
    int          out_h;
    int          rc     = -1;

    if (f_lseek(fp, 0U) != FR_OK)
    {
        *reason = "读取失败";
        return -1;
    }

    /* jd_prepare() saves and restores jd->swap (a TJpgDec extension that
     * controls RGB565 byte order).  The struct is otherwise zero-initialised
     * inside jd_prepare, but swap is left at whatever was on the stack.
     * If it happens to be non-zero, TJpgDec swaps high/low bytes of every
     * RGB565 pixel, producing garbled colours on the ST7789 (which expects
     * standard, non-swapped RGB565 via SPI 16-bit mode — the same layout
     * the BMP path and the NES palette use).  Pin it to zero explicitly. */
    jd.swap = 0U;

    /* Borrow the shared SRAM pool for TJpgDec's work area.  RAM_D2 is the
     * first choice (it has ~286 kB free while this page is active); DTCM is
     * the fallback.  The block is released on every exit path below. */
    pool = sram_alloc(region, IMG_JPEG_POOL, 4U);
    if (pool == NULL)
    {
        region = SRAM_REGION_DTCM;
        pool   = sram_alloc(region, IMG_JPEG_POOL, 4U);
    }
    if (pool == NULL)
    {
        *reason = "内存不足";
        return -1;
    }

    ctx.fp = fp;
    ctx.ox = 0;
    ctx.oy = 0;

    res = jd_prepare(&jd, jpg_in, pool, (size_t)IMG_JPEG_POOL, &ctx);
    if (res != JDR_OK)
    {
        *reason = jpg_reason(res);
        goto done;
    }

    /* TJpgDec descales by 1, 1/2, 1/4 or 1/8 only.  Take the first step that
     * fits; if even 1/8 is oversized the picture is centre-cropped, which
     * beats refusing to show a 12 MP holiday snap at all. */
    while ((scale < 3U) &&
           ((((int)jd.width >> scale)  > IMG_W) ||
            (((int)jd.height >> scale) > IMG_H)))
    {
        scale++;
    }

    out_w = (int)jd.width  >> scale;
    out_h = (int)jd.height >> scale;

    ctx.ox = (IMG_W - out_w) / 2;               /* negative == crop */
    ctx.oy = (IMG_H - out_h) / 2;

    frame_clear();

    res = jd_decomp(&jd, jpg_out, scale);
    if (res != JDR_OK)
    {
        *reason = jpg_reason(res);
        goto done;
    }

    info->src_w     = (int)jd.width;
    info->src_h     = (int)jd.height;
    info->out_w     = (out_w > IMG_W) ? IMG_W : out_w;
    info->out_h     = (out_h > IMG_H) ? IMG_H : out_h;
    info->scale_num = (out_w * 100) / (int)jd.width;
    info->format    = "JPEG";
    rc              = 0;

done:
    sram_free(region, pool);
    return rc;
}

/*----------------------------------------------------------------------------
 *  Public
 *--------------------------------------------------------------------------*/

int img_decode_file(const char *path, img_info_t *info, const char **reason)
{
    FIL      file;
    uint8_t  magic[4];
    UINT     br = 0U;
    FRESULT  fr;
    int      rc;

    static const char *dummy = "未知错误";

    if (reason == NULL)
    {
        reason = &dummy;
    }
    *reason = "未知错误";

    if ((path == NULL) || (info == NULL))
    {
        *reason = "参数错误";
        return -1;
    }

    (void)memset(info, 0, sizeof(*info));

    fr = f_open(&file, path, FA_READ);
    if (fr != FR_OK)
    {
        *reason = "文件打不开";
        return -1;
    }

    info->bytes = (uint32_t)f_size(&file);

    if ((f_read(&file, magic, sizeof(magic), &br) != FR_OK) ||
        (br != sizeof(magic)))
    {
        (void)f_close(&file);
        *reason = "文件太小";
        return -1;
    }

    if ((magic[0] == 0x42U) && (magic[1] == 0x4DU))             /* "BM" */
    {
        rc = decode_bmp(&file, info, reason);
    }
    else if ((magic[0] == 0xFFU) && (magic[1] == 0xD8U))        /* SOI */
    {
        rc = decode_jpeg(&file, info, reason);
    }
    else
    {
        *reason = "不是图片文件";
        rc = -1;
    }

    (void)f_close(&file);

    if (rc == 0)
    {
        printf("[IMG ] %s %s %dx%d -> %dx%d (%lu B)\r\n",
               path, info->format, info->src_w, info->src_h,
               info->out_w, info->out_h, (unsigned long)info->bytes);
    }
    else
    {
        printf("[IMG ] %s failed: %s\r\n", path, *reason);
    }

    return rc;
}

uint16_t *img_framebuffer(void)
{
    return s_fb;
}

void img_blit(void)
{
    uint16_t y;

    for (y = 0U; y < (uint16_t)IMG_H; y = (uint16_t)(y + IMG_BAND_LINES))
    {
        uint16_t rows = IMG_BAND_LINES;

        if ((uint16_t)(y + rows) > (uint16_t)IMG_H)
        {
            rows = (uint16_t)((uint16_t)IMG_H - y);
        }

        LCD_CopyBuffer(0U, y, (uint16_t)IMG_W, rows,
                       &s_fb[(uint32_t)y * IMG_W]);
    }
}
