/**
  ******************************************************************************
  * @file    app_image.c
  * @brief   JPEG -> 240x240 RGB565 pipeline (crop first, then scale).
  ******************************************************************************
  */

#include "app_image.h"
#include "bsp_log.h"

#include "ff.h"
#include "tjpgd.h"

#include <string.h>

/* ---------------------------------------------------------------- defines */

#define FB_W                OLED_WIDTH      /* 240 */
#define FB_H                OLED_HEIGHT     /* 240 */

/*
 * TJPGD_WORKSPACE_SIZE (from tjpgdcnf.h) assumes the stock JD_SZBUF of 512.
 * We enlarged the stream buffer, so add the difference back plus some slack.
 */
#define JPEG_WORK_SIZE      (TJPGD_WORKSPACE_SIZE + JD_SZBUF + 512)

/* ---------------------------------------------------------------- statics */

/* 240*240*2 = 115200 bytes. Placed in AXI SRAM by the linker script. */
static uint16_t s_framebuffer[FB_W * FB_H] __attribute__((aligned(4)));

/* Decoder scratch pool: input buffer + huffman LUTs + MCU work area. */
static uint8_t  s_jpeg_work[JPEG_WORK_SIZE] __attribute__((aligned(8)));

/* Per-decode context handed to TJpgDec through JDEC::device. */
typedef struct
{
    FIL      *fp;
    uint16_t *fb;
    uint16_t  crop_x;       /* origin of the centred square, descaled coords */
    uint16_t  crop_y;
    uint16_t  crop_side;    /* side of that square                           */
} img_ctx_t;

/* ------------------------------------------------------------- callbacks  */

/**
  * @brief  TJpgDec input callback.
  * @note   buff == NULL means "seek forward nbyte bytes".
  */
static size_t jpeg_in_func(JDEC *jd, uint8_t *buff, size_t nbyte)
{
    img_ctx_t *ctx = (img_ctx_t *)jd->device;
    UINT br = 0U;

    if (buff != NULL)
    {
        if (f_read(ctx->fp, buff, (UINT)nbyte, &br) != FR_OK)
        {
            return 0U;
        }
        return (size_t)br;
    }

    /* Skip request */
    {
        FSIZE_t pos = f_tell(ctx->fp) + (FSIZE_t)nbyte;

        if (pos > f_size(ctx->fp))
        {
            return 0U;
        }
        return (f_lseek(ctx->fp, pos) == FR_OK) ? nbyte : 0U;
    }
}

/**
  * @brief  TJpgDec output callback: resample one decoded block into the frame.
  *
  * @p rect is expressed in the *descaled* image coordinate system, which is
  * exactly the space the crop rectangle lives in. For every destination pixel
  * we compute its source pixel (gather). Only the destination pixels whose
  * source falls inside @p rect are touched, so each block is visited once and
  * the whole frame ends up fully covered - no seams, no holes.
  */
static int jpeg_out_func(JDEC *jd, void *bitmap, JRECT *rect)
{
    img_ctx_t      *ctx  = (img_ctx_t *)jd->device;
    const uint16_t *src  = (const uint16_t *)bitmap;
    const uint32_t  side = ctx->crop_side;
    const uint32_t  rw   = (uint32_t)(rect->right - rect->left) + 1U;

    /* Block position relative to the crop origin. */
    const int32_t l = (int32_t)rect->left   - (int32_t)ctx->crop_x;
    const int32_t r = (int32_t)rect->right  - (int32_t)ctx->crop_x;
    const int32_t t = (int32_t)rect->top    - (int32_t)ctx->crop_y;
    const int32_t b = (int32_t)rect->bottom - (int32_t)ctx->crop_y;

    int32_t dx0, dx1, dy0, dy1, dx, dy;

    /* Entirely outside the cropped square -> nothing to draw, keep decoding. */
    if ((r < 0) || (b < 0) || (l >= (int32_t)side) || (t >= (int32_t)side))
    {
        return 1;
    }

    /*
     * Destination range.  Mapping is  s = floor(d * side / FB) , therefore
     *   s >= l   <=>  d >= ceil(l * FB / side)
     *   s <= r   <=>  d <  ceil((r+1) * FB / side)
     */
    dx0 = (l <= 0) ? 0 : (int32_t)(((uint32_t)l * FB_W + side - 1U) / side);
    dx1 = (int32_t)(((uint32_t)(r + 1) * FB_W + side - 1U) / side) - 1;
    dy0 = (t <= 0) ? 0 : (int32_t)(((uint32_t)t * FB_H + side - 1U) / side);
    dy1 = (int32_t)(((uint32_t)(b + 1) * FB_H + side - 1U) / side) - 1;

    if (dx1 > (FB_W - 1)) { dx1 = FB_W - 1; }
    if (dy1 > (FB_H - 1)) { dy1 = FB_H - 1; }

    for (dy = dy0; dy <= dy1; dy++)
    {
        uint32_t        sy   = ctx->crop_y + (((uint32_t)dy * side) / FB_H);
        const uint16_t *srow;
        uint16_t       *drow;

        if ((sy < rect->top) || (sy > rect->bottom))
        {
            continue;   /* defensive, should not happen */
        }

        srow = src + (uint32_t)(sy - rect->top) * rw;
        drow = ctx->fb + (uint32_t)dy * FB_W;

        for (dx = dx0; dx <= dx1; dx++)
        {
            uint32_t sx = ctx->crop_x + (((uint32_t)dx * side) / FB_W);

            if ((sx < rect->left) || (sx > rect->right))
            {
                continue;   /* defensive */
            }
            drow[dx] = srow[sx - rect->left];
        }
    }

    return 1;   /* continue decompression */
}

/* ---------------------------------------------------------------- public  */

uint16_t *app_image_framebuffer(void)
{
    return s_framebuffer;
}

const char *app_image_jres_str(int jres)
{
    switch (jres)
    {
        case JDR_OK:     return "OK";
        case JDR_INTR:   return "interrupted";
        case JDR_INP:    return "input error";
        case JDR_MEM1:   return "out of memory";
        case JDR_MEM2:   return "work area too small";
        case JDR_PAR:    return "bad parameter";
        case JDR_FMT1:   return "broken data";
        case JDR_FMT2:   return "unsupported format";
        case JDR_FMT3:   return "unsupported jpeg";
        default:         return "unknown";
    }
}

GlobalType_t app_image_decode_file(const char *path, app_image_info_t *info)
{
    static FIL  s_file;     /* ~600 bytes with LFN, keep it off the stack */
    static JDEC s_jdec;     /* ~150 bytes                                 */

    img_ctx_t ctx;
    JRESULT   res;
    uint8_t   scale = 0U;
    uint16_t  out_w, out_h, side;
    uint32_t  t0 = HAL_GetTick();

    if (info != NULL)
    {
        memset(info, 0, sizeof(*info));
    }

    memset(&ctx, 0, sizeof(ctx));
    memset(&s_jdec, 0, sizeof(s_jdec));   /* clears jd.swap -> native RGB565 */

    if (f_open(&s_file, path, FA_READ) != FR_OK)
    {
        LOG_E("open %s failed", path);
        return RT_FAIL;
    }

    ctx.fp = &s_file;
    ctx.fb = s_framebuffer;

    res = jd_prepare(&s_jdec, jpeg_in_func, s_jpeg_work, sizeof(s_jpeg_work), &ctx);
    if (res != JDR_OK)
    {
        LOG_E("jd_prepare %s: %s", path, app_image_jres_str((int)res));
        f_close(&s_file);
        if (info != NULL) { info->jres = (int)res; }
        return RT_FAIL;
    }

    /*
     * Pick the strongest descale that still leaves at least 240x240 to crop
     * from. Letting the decoder shrink is far cheaper than resampling later.
     */
    while (scale < 3U)
    {
        uint16_t nw = (uint16_t)(s_jdec.width  >> (scale + 1U));
        uint16_t nh = (uint16_t)(s_jdec.height >> (scale + 1U));
        uint16_t ns = (nw < nh) ? nw : nh;

        if (ns < FB_W)
        {
            break;
        }
        scale++;
    }

    out_w = (uint16_t)(s_jdec.width  >> scale);
    out_h = (uint16_t)(s_jdec.height >> scale);
    if ((out_w == 0U) || (out_h == 0U))
    {
        LOG_E("degenerate size %ux%u", out_w, out_h);
        f_close(&s_file);
        return RT_FAIL;
    }

    /* Centre crop to a square. */
    side          = (out_w < out_h) ? out_w : out_h;
    ctx.crop_side = side;
    ctx.crop_x    = (uint16_t)((out_w - side) / 2U);
    ctx.crop_y    = (uint16_t)((out_h - side) / 2U);

    memset(s_framebuffer, 0, sizeof(s_framebuffer));

    res = jd_decomp(&s_jdec, jpeg_out_func, scale);
    f_close(&s_file);

    if (info != NULL)
    {
        info->src_width  = s_jdec.width;
        info->src_height = s_jdec.height;
        info->out_width  = out_w;
        info->out_height = out_h;
        info->crop_side  = side;
        info->scale      = scale;
        info->jres       = (int)res;
        info->elapsed_ms = HAL_GetTick() - t0;
    }

    if (res != JDR_OK)
    {
        LOG_E("jd_decomp %s: %s", path, app_image_jres_str((int)res));
        return RT_FAIL;
    }

    return RT_OK;
}
