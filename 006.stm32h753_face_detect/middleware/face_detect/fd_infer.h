/**
 * @file    fd_infer.h
 * @brief   CMSIS-NN face detector: 96x96 int8 grey in, up to FD_MAX_BOXES
 *          face boxes out.
 *
 * The network is a CenterNet-style anchor-free detector.  A 13 layer
 * MobileNet-ish backbone reduces 96x96x1 to a 12x12x96 feature map (stride 8),
 * three 1x1 heads then predict per cell:
 *
 *      heatmap  12x12x1   centre confidence  (logit, sigmoid on MCU)
 *      size     12x12x2   w/96, h/96
 *      offset   12x12x2   sub-cell centre offset in [0,1)
 *
 * Decoding is the standard CenterNet recipe: 3x3 max-pool NMS on the heatmap,
 * keep local maxima above a threshold, turn each peak into a box.
 *
 * Everything about the graph (shapes, quantisation, weights) comes from the
 * generated fd_model_data.c, so retraining only regenerates that one file.
 */
#ifndef __FD_INFER_H
#define __FD_INFER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

/* --------------------------------------------------------------- geometry */
#define FD_INPUT_W          96
#define FD_INPUT_H          96
#define FD_INPUT_C          1
#define FD_INPUT_PIXELS     (FD_INPUT_W * FD_INPUT_H)

#define FD_STRIDE           8
#define FD_GRID_W           (FD_INPUT_W / FD_STRIDE)   /* 12 */
#define FD_GRID_H           (FD_INPUT_H / FD_STRIDE)   /* 12 */
#define FD_GRID_CELLS       (FD_GRID_W * FD_GRID_H)

#define FD_MAX_BOXES        8

/* Fixed by construction in tools/fd_export.py: the app hands over luma 0..255
 * and the tensor is simply luma-128, i.e. scale 1/255 with zero point -128. */
#define FD_INPUT_ZERO_POINT (-128)

/* ------------------------------------------------------------ layer table */
typedef enum
{
    FD_LAYER_CONV = 0,      /* regular convolution                          */
    FD_LAYER_DW   = 1,      /* depthwise convolution, channel multiplier 1  */
} fd_layer_kind_t;

/**
 * @brief One quantised convolution, described exactly the way CMSIS-NN wants
 *        it.  @c mult / @c shift are per output channel (TFLite convention).
 */
typedef struct
{
    const char     *name;
    uint8_t         kind;           /* fd_layer_kind_t                      */
    uint8_t         k;              /* square kernel size                   */
    uint8_t         stride;
    uint8_t         pad;

    uint16_t        in_h, in_w, in_c;
    uint16_t        out_h, out_w, out_c;

    int32_t         input_offset;   /* -input_zero_point                    */
    int32_t         output_offset;  /* +output_zero_point                   */
    int32_t         act_min;        /* clamped activation range, int8       */
    int32_t         act_max;

    const int8_t   *weights;
    const int32_t  *bias;
    const int32_t  *mult;
    const int32_t  *shift;
} fd_layer_t;

/* ------------------------------------------------------------------ boxes */
typedef struct
{
    int16_t x, y, w, h;     /* box in 96x96 input pixel coordinates */
    uint8_t score;          /* sigmoid(heatmap) scaled to 0..255    */
} fd_box_t;

typedef struct
{
    fd_box_t box[FD_MAX_BOXES];
    uint8_t  count;
    uint8_t  peak;          /* strongest score seen this frame, 0..255 */
    uint32_t infer_us;      /* pure network time, microseconds         */
} fd_result_t;

/* -------------------------------------------------------------------- API */
/** One-time setup; also enables the DWT cycle counter used for timing. */
void fd_init(void);

/**
 * @brief  The 96x96 int8 input tensor.
 *
 * The caller writes the downsampled camera image straight into this buffer
 * (luma - 128), which saves a full frame copy before every inference.
 */
int8_t *fd_input_buffer(void);

/**
 * @brief  Run the network on fd_input_buffer() and decode the boxes.
 * @param  res  receives the detections, sorted by descending score.
 * @return RT_OK on success, RT_FAIL if a CMSIS-NN kernel rejected its inputs.
 */
GlobalType_t fd_run(fd_result_t *res);

/**
 * @brief  192x192 RGB565 -> 96x96 int8 luma, straight into the input tensor.
 *
 * A 2x2 box filter halves the frame and converts to luma in one pass, which
 * is exactly the 2:1 ratio between the captured frame and the network input,
 * so a detected box at (x,y,w,h) maps to the displayed frame by simply
 * doubling every coordinate.
 *
 * @param src         source frame, RGB565, row major
 * @param src_stride  source pixels per row (192 here)
 * @param dst         FD_INPUT_PIXELS int8 values, normally fd_input_buffer()
 */
void fd_preprocess_rgb565(const uint16_t *src, uint32_t src_stride, int8_t *dst);

/** Detection threshold as a 0..255 score, default FD_DEFAULT_THRESHOLD. */
void fd_set_threshold(uint8_t score);
uint8_t fd_get_threshold(void);

#define FD_DEFAULT_THRESHOLD  115      /* ~0.45 after sigmoid */

/** Raw 12x12 heatmap of the last frame as 0..255 scores (debug/tuning). */
const uint8_t *fd_last_heatmap(void);

#ifdef __cplusplus
}
#endif
#endif /* __FD_INFER_H */
