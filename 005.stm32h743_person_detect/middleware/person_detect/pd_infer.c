/**
 * @file    pd_infer.c
 * @brief   CMSIS-NN runtime for the compiled-in person detection model.
 *
 * Execution plan
 * ---------------------------------------------------------------------------
 *  layer  0 .. 25   MobileNet backbone, ping-pong between two arena buffers
 *  layer 26         last pointwise conv -> 3x3x256 feature map
 *  (here)           CAM read-out + global average pool 3x3x256 -> 1x1x256
 *  layer 27         1x1 classifier -> 2 logits
 *
 * All intermediate tensors live in the cacheable AXI SRAM window at
 * 0x24040000 (.axi_ram), never in the DCMI DMA window, so no cache
 * maintenance is required here.
 */
#include <math.h>
#include <string.h>

#include "main.h"
#include "arm_nnfunctions.h"
#include "pd_infer.h"

/* ------------------------------------------------------------------ memory */

/* One extra guard page per buffer: some CMSIS-NN kernels are documented to
 * touch a few bytes past the nominal tensor end for alignment reasons. */
#define PD_ARENA_GUARD      64
#define PD_ARENA_STRIDE     (PD_TENSOR_ARENA_SIZE + PD_ARENA_GUARD)

AXI_RAM static int8_t s_arena[2][PD_ARENA_STRIDE];
AXI_RAM static int8_t s_scratch[PD_SCRATCH_SIZE + PD_ARENA_GUARD];

static int8_t  s_pooled[PD_FEATURE_C] __attribute__((aligned(4)));
static int8_t  s_logits[8]            __attribute__((aligned(4)));

static float   s_threshold = 0.60f;
static uint8_t s_ready;

/* --------------------------------------------------------------- utilities */

static void dwt_enable(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
#if defined(DWT_LAR_KEY) || 1
    /* Unlock the DWT on cores that implement the software lock. Writing the
     * key to a read-only register is harmless on the ones that do not. */
    *(volatile uint32_t *)(DWT_BASE + 0xFB0U) = 0xC5ACCE55U;
#endif
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/* ------------------------------------------------------------ layer runner */

static arm_status run_layer(const pd_layer_t *L,
                            const int8_t *input,
                            int8_t *output,
                            cmsis_nn_context *ctx)
{
    cmsis_nn_dims in_dims   = { 1, L->in_h,  L->in_w,  L->in_c  };
    cmsis_nn_dims out_dims  = { 1, L->out_h, L->out_w, L->out_c };
    cmsis_nn_dims bias_dims = { 1, 1, 1, L->out_c };

    cmsis_nn_per_channel_quant_params quant;
    quant.multiplier = (int32_t *)L->multiplier;
    quant.shift      = (int32_t *)L->shift;

    if (L->type == PD_LAYER_DEPTHWISE)
    {
        /* CMSIS-NN depthwise filter layout is [1, H, W, C_OUT] - exactly the
         * TFLite layout the weights were exported in. */
        cmsis_nn_dims filter_dims = { 1, L->kernel, L->kernel, L->out_c };
        cmsis_nn_dw_conv_params p;

        p.input_offset   = L->input_offset;
        p.output_offset  = L->output_offset;
        p.ch_mult        = L->ch_mult;
        p.stride.w       = L->stride;
        p.stride.h       = L->stride;
        p.padding.w      = L->pad;
        p.padding.h      = L->pad;
        p.dilation.w     = 1;
        p.dilation.h     = 1;
        p.activation.min = L->act_min;
        p.activation.max = L->act_max;

        return arm_depthwise_conv_wrapper_s8(ctx, &p, &quant,
                                             &in_dims, input,
                                             &filter_dims, L->weights,
                                             &bias_dims, L->bias,
                                             &out_dims, output);
    }

    /* Regular (here always 1x1 pointwise) convolution: [C_OUT, H, W, C_IN]. */
    {
        cmsis_nn_dims filter_dims = { L->out_c, L->kernel, L->kernel, L->in_c };
        cmsis_nn_conv_params p;

        p.input_offset   = L->input_offset;
        p.output_offset  = L->output_offset;
        p.stride.w       = L->stride;
        p.stride.h       = L->stride;
        p.padding.w      = L->pad;
        p.padding.h      = L->pad;
        p.dilation.w     = 1;
        p.dilation.h     = 1;
        p.activation.min = L->act_min;
        p.activation.max = L->act_max;

        return arm_convolve_wrapper_s8(ctx, &p, &quant,
                                       &in_dims, input,
                                       &filter_dims, L->weights,
                                       &bias_dims, L->bias,
                                       &out_dims, output);
    }
}

/* ------------------------------------------------------- global avg pooling
 * TFLite int8 AVERAGE_POOL_2D with identical input/output quantisation:
 * accumulate, round half away from zero, saturate.  Verified bit exact
 * against the TFLite reference kernel by tools/validate_model.py.
 */
static void global_avgpool(const int8_t *feat, int8_t *out)
{
    const int32_t cells = PD_FEATURE_H * PD_FEATURE_W;
    const int32_t half  = cells / 2;

    for (int32_t c = 0; c < PD_FEATURE_C; c++)
    {
        int32_t acc = 0;
        for (int32_t p = 0; p < cells; p++)
        {
            acc += feat[p * PD_FEATURE_C + c];
        }
        acc = (acc > 0) ? ((acc + half) / cells) : -((-acc + half) / cells);
        if (acc > 127)  acc = 127;
        if (acc < -128) acc = -128;
        out[c] = (int8_t)acc;
    }
}

/* ---------------------------------------------------- class activation map */

static void cam_localise(const int8_t *feat, pd_result_t *res)
{
    int64_t raw[PD_CAM_CELLS];
    int64_t lo, hi;
    float   w[PD_CAM_CELLS];
    float   sum = 0.0f, cx = 0.0f, cy = 0.0f, vx = 0.0f, vy = 0.0f;
    float   thr, span, sx, sy, hw, hh;
    int32_t i, j, c;

    for (i = 0; i < PD_CAM_CELLS; i++)
    {
        const int8_t *v = feat + (int32_t)i * PD_FEATURE_C;
        int64_t acc = 0;
        for (c = 0; c < PD_FEATURE_C; c++)
        {
            /* de-zero-point the activation: (q - zp) is always >= 0 after ReLU */
            acc += (int64_t)pd_cam_w[c] * (int64_t)((int32_t)v[c] - PD_FEATURE_ZP);
        }
        raw[i] = acc;
    }

    lo = hi = raw[0];
    for (i = 1; i < PD_CAM_CELLS; i++)
    {
        if (raw[i] < lo) lo = raw[i];
        if (raw[i] > hi) hi = raw[i];
    }

    span = (float)(hi - lo);
    if (span <= 0.0f)
    {
        for (i = 0; i < PD_CAM_CELLS; i++) res->cam[i] = 0;
        res->box_valid = 0;
        return;
    }

    for (i = 0; i < PD_CAM_CELLS; i++)
    {
        float n = (float)(raw[i] - lo) / span;      /* 0.0 .. 1.0 */
        res->cam[i] = (uint8_t)(n * 255.0f + 0.5f);
        w[i] = n;
    }

    /* Keep only the upper half of the dynamic range: with a 3x3 grid a plain
     * centroid would always be dragged towards the middle cell. */
    thr = 0.5f;
    for (i = 0; i < PD_CAM_CELLS; i++)
    {
        w[i] = (w[i] > thr) ? (w[i] - thr) : 0.0f;
        sum += w[i];
    }
    if (sum <= 1e-6f)
    {
        res->box_valid = 0;
        return;
    }

    for (i = 0; i < PD_CAM_H; i++)
    {
        for (j = 0; j < PD_CAM_W; j++)
        {
            float k = w[i * PD_CAM_W + j];
            cx += k * ((float)j + 0.5f);
            cy += k * ((float)i + 0.5f);
        }
    }
    cx /= sum;
    cy /= sum;

    for (i = 0; i < PD_CAM_H; i++)
    {
        for (j = 0; j < PD_CAM_W; j++)
        {
            float k  = w[i * PD_CAM_W + j];
            float dx = (float)j + 0.5f - cx;
            float dy = (float)i + 0.5f - cy;
            vx += k * dx * dx;
            vy += k * dy * dy;
        }
    }
    sx = sqrtf(vx / sum);
    sy = sqrtf(vy / sum);

    /* One CAM cell covers 96/3 = 32 input pixels.  The constant term keeps a
     * single-cell activation from collapsing into a degenerate box. */
    hw = sx * 1.5f + 0.62f;
    hh = sy * 1.5f + 0.62f;
    if (hw > 1.5f) hw = 1.5f;
    if (hh > 1.5f) hh = 1.5f;

    {
        const float cell = (float)PD_INPUT_W / (float)PD_CAM_W;   /* 32.0 */
        float x0 = (cx - hw) * cell;
        float y0 = (cy - hh) * cell;
        float x1 = (cx + hw) * cell;
        float y1 = (cy + hh) * cell;

        if (x0 < 0.0f) x0 = 0.0f;
        if (y0 < 0.0f) y0 = 0.0f;
        if (x1 > (float)PD_INPUT_W) x1 = (float)PD_INPUT_W;
        if (y1 > (float)PD_INPUT_H) y1 = (float)PD_INPUT_H;

        res->x = (int16_t)(x0 + 0.5f);
        res->y = (int16_t)(y0 + 0.5f);
        res->w = (int16_t)(x1 - x0 + 0.5f);
        res->h = (int16_t)(y1 - y0 + 0.5f);
    }
    res->box_valid = (res->w > 3 && res->h > 3) ? 1u : 0u;
}

/* -------------------------------------------------------------- public API */

int pd_init(void)
{
    cmsis_nn_context probe;
    int32_t need_scratch = 0;
    int32_t i;

    dwt_enable();

    (void)probe;

    for (i = 0; i < PD_NUM_LAYERS; i++)
    {
        const pd_layer_t *L = &pd_layers[i];
        int32_t in_sz  = (int32_t)L->in_h  * L->in_w  * L->in_c;
        int32_t out_sz = (int32_t)L->out_h * L->out_w * L->out_c;
        int32_t sz;

        cmsis_nn_dims in_dims  = { 1, L->in_h,  L->in_w,  L->in_c  };
        cmsis_nn_dims out_dims = { 1, L->out_h, L->out_w, L->out_c };

        if (in_sz > PD_TENSOR_ARENA_SIZE || out_sz > PD_TENSOR_ARENA_SIZE)
        {
            return -1;                      /* arena too small for this graph */
        }

        if (L->type == PD_LAYER_DEPTHWISE)
        {
            cmsis_nn_dims filter_dims = { 1, L->kernel, L->kernel, L->out_c };
            cmsis_nn_dw_conv_params p = { 0 };
            p.ch_mult    = L->ch_mult;
            p.stride.w   = L->stride;
            p.stride.h   = L->stride;
            p.padding.w  = L->pad;
            p.padding.h  = L->pad;
            p.dilation.w = 1;
            p.dilation.h = 1;
            sz = arm_depthwise_conv_wrapper_s8_get_buffer_size(&p, &in_dims,
                                                               &filter_dims,
                                                               &out_dims);
        }
        else
        {
            cmsis_nn_dims filter_dims = { L->out_c, L->kernel, L->kernel, L->in_c };
            cmsis_nn_conv_params p = { 0 };
            p.stride.w   = L->stride;
            p.stride.h   = L->stride;
            p.padding.w  = L->pad;
            p.padding.h  = L->pad;
            p.dilation.w = 1;
            p.dilation.h = 1;
            sz = arm_convolve_wrapper_s8_get_buffer_size(&p, &in_dims,
                                                         &filter_dims,
                                                         &out_dims);
        }
        if (sz > need_scratch) need_scratch = sz;
    }

    if (need_scratch > (int32_t)PD_SCRATCH_SIZE)
    {
        return -2;                          /* scratch buffer too small */
    }

    s_ready = 1u;
    return 0;
}

int8_t *pd_input(void)
{
    return s_arena[0];
}

void pd_set_threshold(float th)
{
    if (th < 0.0f) th = 0.0f;
    if (th > 1.0f) th = 1.0f;
    s_threshold = th;
}

float pd_get_threshold(void)
{
    return s_threshold;
}

int pd_run(pd_result_t *res)
{
    cmsis_nn_context ctx;
    uint32_t t0;
    int32_t  i, cur = 0;
    float    diff;

    if (!s_ready || res == NULL) return -1;

    ctx.buf  = s_scratch;
    ctx.size = (int32_t)PD_SCRATCH_SIZE;

    memset(res, 0, sizeof(*res));
    t0 = DWT->CYCCNT;

    /* --- backbone ------------------------------------------------------- */
    for (i = 0; i < PD_NUM_LAYERS - 1; i++)
    {
        if (run_layer(&pd_layers[i], s_arena[cur], s_arena[cur ^ 1], &ctx)
            != ARM_MATH_SUCCESS)
        {
            return -(10 + i);
        }
        cur ^= 1;
    }

    /* --- CAM + global average pool -------------------------------------- */
    cam_localise(s_arena[cur], res);
    global_avgpool(s_arena[cur], s_pooled);

    /* --- classifier ------------------------------------------------------ */
    if (run_layer(&pd_layers[PD_NUM_LAYERS - 1], s_pooled, s_logits, &ctx)
        != ARM_MATH_SUCCESS)
    {
        return -9;
    }

    res->logit[0] = s_logits[0];
    res->logit[1] = s_logits[1];

    /* Both logits share one scale/zero point, so the zero point cancels in
     * the difference and the softmax over two classes becomes a sigmoid. */
    diff = PD_LOGIT_SCALE * (float)((int32_t)s_logits[1] - (int32_t)s_logits[0]);
    res->score  = 1.0f / (1.0f + expf(-diff));
    res->person = (res->score >= s_threshold) ? 1u : 0u;
    if (!res->person) res->box_valid = 0u;

    res->cycles = DWT->CYCCNT - t0;
    res->us     = res->cycles / (SystemCoreClock / 1000000u);
    return 0;
}

/* ------------------------------------------------------------- preprocess */

/* Luma in Q8: 0.299*R + 0.587*G + 0.114*B with the RGB565 fields expanded to
 * 8 bit by a left shift (r<<3, g<<2, b<<3):
 *   77*(r<<3) + 150*(g<<2) + 29*(b<<3) = 616*r + 600*g + 232*b            */
static inline uint32_t rgb565_luma_q8(uint16_t p)
{
    return 616u * ((uint32_t)(p >> 11) & 0x1Fu)
         + 600u * ((uint32_t)(p >> 5)  & 0x3Fu)
         + 232u * ((uint32_t)p         & 0x1Fu);
}

void pd_preprocess_rgb565(const uint16_t *src, uint32_t src_stride,
                          int8_t *dst, uint8_t *luma_out)
{
    uint32_t y, x;

    for (y = 0; y < PD_INPUT_H; y++)
    {
        const uint16_t *r0 = src + (uint32_t)(2u * y) * src_stride;
        const uint16_t *r1 = r0 + src_stride;

        for (x = 0; x < PD_INPUT_W; x++)
        {
            uint32_t acc = rgb565_luma_q8(r0[0]) + rgb565_luma_q8(r0[1])
                         + rgb565_luma_q8(r1[0]) + rgb565_luma_q8(r1[1]);
            uint32_t luma = acc >> 10;                  /* /4 average, Q8 -> Q0 */

            if (luma > 255u) luma = 255u;
            if (luma_out != NULL) luma_out[y * PD_INPUT_W + x] = (uint8_t)luma;
            dst[y * PD_INPUT_W + x] = (int8_t)((int32_t)luma - 128);

            r0 += 2;
            r1 += 2;
        }
    }
}
