/**
 * @file    fd_infer.c
 * @brief   CMSIS-NN runtime for the CenterNet-style face detector.
 *
 * Memory plan (all in cacheable AXI SRAM, CPU only - the DCMI DMA window is a
 * different MPU region):
 *
 *   s_input   96*96          =   9216 B   filled by the app, never overwritten
 *   s_arena   2 * ARENA_HALF =  73728 B   ping-pong for the backbone tensors
 *   heads     144*(1+2+2)    =    720 B   int8 head outputs
 *   s_scratch                =   4096 B   CMSIS-NN im2col / dw workspace
 *
 * The whole graph is driven from the generated table in fd_model_data.c, so
 * this file never has to change when the network is retrained.
 */
#include "fd_infer.h"
#include "fd_model_data.h"
#include "logger.h"

#include "arm_nnfunctions.h"

#include <math.h>
#include <string.h>

/* --------------------------------------------------------------- storage */
AXI_RAM static int8_t  s_input[FD_INPUT_PIXELS];
AXI_RAM static int8_t  s_arena[2][FD_ARENA_HALF];
AXI_RAM static int8_t  s_hm[FD_GRID_CELLS * 1];
AXI_RAM static int8_t  s_wh[FD_GRID_CELLS * 2];
AXI_RAM static int8_t  s_off[FD_GRID_CELLS * 2];

/* Big enough for every layer: im2col needs 2*in_c*k*k*2 bytes (max 384) and
 * the optimised depthwise kernel needs in_c*k*k*2 bytes (max 1728). */
#define FD_SCRATCH_BYTES    4096
AXI_RAM static int16_t s_scratch[FD_SCRATCH_BYTES / 2];

static uint8_t s_heatmap[FD_GRID_CELLS];    /* 0..255 debug view */
static uint8_t s_threshold = FD_DEFAULT_THRESHOLD;
static uint8_t s_ready;

/* ------------------------------------------------------------------ utils */
static void dwt_enable(void)
{
    /* Unlock the DWT on Cortex-M7 (no-op where the register is not present)
     * and start the free running cycle counter. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    *(volatile uint32_t *)0xE0001FB0u = 0xC5ACCE55u;   /* DWT_LAR */
    DWT->CYCCNT = 0u;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

static inline uint32_t dwt_cycles(void)
{
    return DWT->CYCCNT;
}

static inline float fd_sigmoid(float x)
{
    return 1.0f / (1.0f + expf(-x));
}

static inline int16_t clamp16(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return (int16_t)v;
}

/* ------------------------------------------------------- one CMSIS-NN layer */
static arm_status fd_run_layer(const fd_layer_t *l,
                               const int8_t *in, int8_t *out)
{
    cmsis_nn_context ctx;
    cmsis_nn_dims    input_dims, filter_dims, bias_dims, output_dims;
    cmsis_nn_per_channel_quant_params qp;

    ctx.buf  = s_scratch;
    ctx.size = FD_SCRATCH_BYTES;

    qp.multiplier = (int32_t *)l->mult;
    qp.shift      = (int32_t *)l->shift;

    input_dims.n  = 1;
    input_dims.h  = l->in_h;
    input_dims.w  = l->in_w;
    input_dims.c  = l->in_c;

    output_dims.n = 1;
    output_dims.h = l->out_h;
    output_dims.w = l->out_w;
    output_dims.c = l->out_c;

    bias_dims.n = bias_dims.h = bias_dims.w = 1;
    bias_dims.c = l->out_c;

    if (l->kind == FD_LAYER_DW)
    {
        cmsis_nn_dw_conv_params dw;

        /* CMSIS-NN depthwise filter layout is [1, H, W, C_OUT]. */
        filter_dims.n = 1;
        filter_dims.h = l->k;
        filter_dims.w = l->k;
        filter_dims.c = l->out_c;

        dw.input_offset  = l->input_offset;
        dw.output_offset = l->output_offset;
        dw.ch_mult       = 1;
        dw.stride.w      = l->stride;
        dw.stride.h      = l->stride;
        dw.padding.w     = l->pad;
        dw.padding.h     = l->pad;
        dw.dilation.w    = 1;
        dw.dilation.h    = 1;
        dw.activation.min = l->act_min;
        dw.activation.max = l->act_max;

        return arm_depthwise_conv_wrapper_s8(&ctx, &dw, &qp,
                                             &input_dims,  in,
                                             &filter_dims, l->weights,
                                             &bias_dims,   l->bias,
                                             &output_dims, out);
    }
    else
    {
        cmsis_nn_conv_params cp;

        /* Regular convolution filter layout is [C_OUT, HK, WK, C_IN]. */
        filter_dims.n = l->out_c;
        filter_dims.h = l->k;
        filter_dims.w = l->k;
        filter_dims.c = l->in_c;

        cp.input_offset  = l->input_offset;
        cp.output_offset = l->output_offset;
        cp.stride.w      = l->stride;
        cp.stride.h      = l->stride;
        cp.padding.w     = l->pad;
        cp.padding.h     = l->pad;
        cp.dilation.w    = 1;
        cp.dilation.h    = 1;
        cp.activation.min = l->act_min;
        cp.activation.max = l->act_max;

        return arm_convolve_wrapper_s8(&ctx, &cp, &qp,
                                       &input_dims,  in,
                                       &filter_dims, l->weights,
                                       &bias_dims,   l->bias,
                                       &output_dims, out);
    }
}

/* ------------------------------------------------------------------- init */
void fd_init(void)
{
    uint32_t i;
    int32_t  worst = 0;

    dwt_enable();

    /* Sanity check the static scratch against what the kernels actually ask
     * for; a silent overflow here would corrupt neighbouring buffers. */
    for (i = 0; i < FD_NUM_BACKBONE + FD_NUM_HEADS; i++)
    {
        const fd_layer_t *l = (i < FD_NUM_BACKBONE)
                            ? &fd_backbone[i]
                            : &fd_heads[i - FD_NUM_BACKBONE];
        cmsis_nn_dims in_d  = { 1, l->in_h, l->in_w, l->in_c };
        cmsis_nn_dims flt_d;
        int32_t need;

        if (l->kind == FD_LAYER_DW)
        {
            cmsis_nn_dw_conv_params dw;
            memset(&dw, 0, sizeof(dw));
            dw.ch_mult    = 1;
            dw.stride.w   = l->stride;
            dw.stride.h   = l->stride;
            dw.padding.w  = l->pad;
            dw.padding.h  = l->pad;
            dw.dilation.w = 1;
            dw.dilation.h = 1;
            flt_d.n = 1;  flt_d.h = l->k;  flt_d.w = l->k;  flt_d.c = l->out_c;
            need = arm_depthwise_conv_wrapper_s8_get_buffer_size(&dw, &in_d,
                                                                 &flt_d, NULL);
        }
        else
        {
            cmsis_nn_conv_params cp;
            cmsis_nn_dims out_d = { 1, l->out_h, l->out_w, l->out_c };
            memset(&cp, 0, sizeof(cp));
            cp.stride.w   = l->stride;
            cp.stride.h   = l->stride;
            cp.padding.w  = l->pad;
            cp.padding.h  = l->pad;
            cp.dilation.w = 1;
            cp.dilation.h = 1;
            flt_d.n = l->out_c; flt_d.h = l->k; flt_d.w = l->k; flt_d.c = l->in_c;
            need = arm_convolve_wrapper_s8_get_buffer_size(&cp, &in_d,
                                                           &flt_d, &out_d);
        }
        if (need > worst)
        {
            worst = need;
        }
    }

    if (worst > FD_SCRATCH_BYTES)
    {
        PRINT_LOG(LOG_ERROR, HAL_GetTick(),
                  "fd: scratch too small, need %ld have %d",
                  (long)worst, FD_SCRATCH_BYTES);
        s_ready = 0;
        return;
    }

    PRINT_LOG(LOG_INFO, HAL_GetTick(),
              "fd: %d+%d layers, arena %d B, scratch %ld/%d B",
              (int)FD_NUM_BACKBONE, (int)FD_NUM_HEADS,
              (int)(2 * FD_ARENA_HALF), (long)worst, FD_SCRATCH_BYTES);
    s_ready = 1;
}

int8_t *fd_input_buffer(void)
{
    return s_input;
}

/* ------------------------------------------------------------ preprocess */
/** ITU-R BT.601 luma of one RGB565 pixel, returned in Q8 (i.e. 256*luma). */
static inline uint32_t rgb565_luma_q8(uint16_t p)
{
    /* 5/6/5 -> 8 bit by replicating the high bits, then 0.299/0.587/0.114
     * with integer weights 77/150/29 (sum 256). */
    uint32_t r = (uint32_t)((p >> 11) & 0x1Fu);
    uint32_t g = (uint32_t)((p >> 5)  & 0x3Fu);
    uint32_t b = (uint32_t)(p         & 0x1Fu);

    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);

    return 77u * r + 150u * g + 29u * b;    /* Q8 */
}

void fd_preprocess_rgb565(const uint16_t *src, uint32_t src_stride, int8_t *dst)
{
    uint32_t y, x;

    if (src == NULL || dst == NULL)
    {
        return;
    }

    for (y = 0; y < FD_INPUT_H; y++)
    {
        const uint16_t *r0 = src + (uint32_t)(2u * y) * src_stride;
        const uint16_t *r1 = r0 + src_stride;

        for (x = 0; x < FD_INPUT_W; x++)
        {
            uint32_t acc = rgb565_luma_q8(r0[0]) + rgb565_luma_q8(r0[1])
                         + rgb565_luma_q8(r1[0]) + rgb565_luma_q8(r1[1]);
            uint32_t luma = acc >> 10;      /* /4 for the box filter, Q8 -> Q0 */

            if (luma > 255u)
            {
                luma = 255u;
            }
            /* The export pipeline fixes the input quantisation at
             * scale 1/255, zero point -128, so the tensor is just luma-128. */
            dst[y * FD_INPUT_W + x] = (int8_t)((int32_t)luma - 128);

            r0 += 2;
            r1 += 2;
        }
    }
}

void fd_set_threshold(uint8_t score)
{
    s_threshold = score;
}

uint8_t fd_get_threshold(void)
{
    return s_threshold;
}

const uint8_t *fd_last_heatmap(void)
{
    return s_heatmap;
}

/* --------------------------------------------------------------- decoding */
/**
 * @brief CenterNet decode: 3x3 max-pool NMS, then peak -> box.
 *
 * A cell survives when it is the strongest of its 3x3 neighbourhood and above
 * the score threshold.  That is enough for the handful of faces this demo has
 * to deal with, and it costs nothing compared to a real IoU NMS.
 */
static void fd_decode(fd_result_t *res)
{
    int row, col;
    uint8_t peak_max = 0;

    res->count = 0;

    /* 1. dequantise the heatmap into 0..255 scores */
    for (row = 0; row < FD_GRID_H; row++)
    {
        for (col = 0; col < FD_GRID_W; col++)
        {
            int   idx  = row * FD_GRID_W + col;
            float logit = ((float)s_hm[idx] - (float)FD_HM_ZP) * FD_HM_SCALE;
            float p     = fd_sigmoid(logit);
            int   score = (int)(p * 255.0f + 0.5f);

            if (score < 0)   score = 0;
            if (score > 255) score = 255;
            s_heatmap[idx] = (uint8_t)score;
            if (score > peak_max)
            {
                peak_max = (uint8_t)score;
            }
        }
    }
    res->peak = peak_max;

    /* 2. 3x3 max-pool NMS + box regression */
    for (row = 0; row < FD_GRID_H; row++)
    {
        for (col = 0; col < FD_GRID_W; col++)
        {
            int idx = row * FD_GRID_W + col;
            uint8_t score = s_heatmap[idx];
            int dy, dx, is_peak = 1;
            float ox, oy, bw, bh, cx, cy;
            int32_t x0, y0, w, h;
            int slot, k;

            if (score < s_threshold)
            {
                continue;
            }

            for (dy = -1; dy <= 1 && is_peak; dy++)
            {
                for (dx = -1; dx <= 1; dx++)
                {
                    int ny = row + dy, nx = col + dx;
                    if ((dy == 0 && dx == 0) ||
                        ny < 0 || ny >= FD_GRID_H ||
                        nx < 0 || nx >= FD_GRID_W)
                    {
                        continue;
                    }
                    if (s_heatmap[ny * FD_GRID_W + nx] > score)
                    {
                        is_peak = 0;
                        break;
                    }
                }
            }
            if (!is_peak)
            {
                continue;
            }

            /* offset head is sigmoid-free: it was trained in [0,1) directly */
            ox = ((float)s_off[idx * 2 + 0] - (float)FD_OFF_ZP) * FD_OFF_SCALE;
            oy = ((float)s_off[idx * 2 + 1] - (float)FD_OFF_ZP) * FD_OFF_SCALE;
            bw = ((float)s_wh[idx * 2 + 0]  - (float)FD_WH_ZP)  * FD_WH_SCALE;
            bh = ((float)s_wh[idx * 2 + 1]  - (float)FD_WH_ZP)  * FD_WH_SCALE;

            if (ox < 0.0f) { ox = 0.0f; }
            if (ox > 1.0f) { ox = 1.0f; }
            if (oy < 0.0f) { oy = 0.0f; }
            if (oy > 1.0f) { oy = 1.0f; }
            if (bw <= 0.0f || bh <= 0.0f)
            {
                continue;
            }

            cx = ((float)col + ox) * (float)FD_STRIDE;
            cy = ((float)row + oy) * (float)FD_STRIDE;
            w  = (int32_t)(bw * (float)FD_INPUT_W + 0.5f);
            h  = (int32_t)(bh * (float)FD_INPUT_H + 0.5f);
            x0 = (int32_t)(cx + 0.5f) - w / 2;
            y0 = (int32_t)(cy + 0.5f) - h / 2;

            /* clip to the input frame */
            if (x0 < 0) { w += x0; x0 = 0; }
            if (y0 < 0) { h += y0; y0 = 0; }
            if (x0 + w > FD_INPUT_W) w = FD_INPUT_W - x0;
            if (y0 + h > FD_INPUT_H) h = FD_INPUT_H - y0;
            if (w < 6 || h < 6)         /* smaller than that is always noise */
            {
                continue;
            }

            /* 3. insertion sort into the result list, best score first */
            slot = res->count;
            for (k = 0; k < res->count; k++)
            {
                if (score > res->box[k].score)
                {
                    slot = k;
                    break;
                }
            }
            if (slot >= FD_MAX_BOXES)
            {
                continue;
            }
            for (k = (res->count < FD_MAX_BOXES ? res->count : FD_MAX_BOXES - 1);
                 k > slot; k--)
            {
                res->box[k] = res->box[k - 1];
            }
            res->box[slot].x     = clamp16(x0, 0, FD_INPUT_W - 1);
            res->box[slot].y     = clamp16(y0, 0, FD_INPUT_H - 1);
            res->box[slot].w     = clamp16(w, 1, FD_INPUT_W);
            res->box[slot].h     = clamp16(h, 1, FD_INPUT_H);
            res->box[slot].score = score;
            if (res->count < FD_MAX_BOXES)
            {
                res->count++;
            }
        }
    }
}

/* -------------------------------------------------------------------- run */
GlobalType_t fd_run(fd_result_t *res)
{
    uint32_t i, t0, cycles;
    const int8_t *cur;
    int8_t *dst;
    uint8_t half = 0;

    if (res == NULL)
    {
        return RT_FAIL;
    }
    memset(res, 0, sizeof(*res));

    if (!s_ready)
    {
        return RT_FAIL;
    }

    t0  = dwt_cycles();
    cur = s_input;

    for (i = 0; i < FD_NUM_BACKBONE; i++)
    {
        dst = s_arena[half];
        if (fd_run_layer(&fd_backbone[i], cur, dst) != ARM_MATH_SUCCESS)
        {
            PRINT_LOG(LOG_ERROR, HAL_GetTick(), "fd: layer %s failed",
                      fd_backbone[i].name);
            return RT_FAIL;
        }
        cur  = dst;
        half ^= 1u;
    }

    /* All three heads read the same final backbone tensor. */
    if (fd_run_layer(&fd_heads[0], cur, s_hm)  != ARM_MATH_SUCCESS ||
        fd_run_layer(&fd_heads[1], cur, s_wh)  != ARM_MATH_SUCCESS ||
        fd_run_layer(&fd_heads[2], cur, s_off) != ARM_MATH_SUCCESS)
    {
        PRINT_LOG(LOG_ERROR, HAL_GetTick(), "fd: head failed");
        return RT_FAIL;
    }

    cycles = dwt_cycles() - t0;
    res->infer_us = cycles / (SystemCoreClock / 1000000u);

    fd_decode(res);
    return RT_OK;
}
