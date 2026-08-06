/**
 * @file    pd_infer.h
 * @brief   Person-detection inference on CMSIS-NN.
 *
 * Model: Visual Wake Words / MobileNet-v1 0.25, 96x96x1 int8, 2 classes.
 * The graph is stored in pd_model_data.c as a flat list of 28 conv /
 * depthwise-conv layers plus a global average pool that sits between the
 * backbone (layer 26) and the 1x1 classifier (layer 27).
 *
 * Besides the classification score the module also derives a coarse bounding
 * box with Class Activation Mapping: the 3x3x256 feature map is projected on
 * the (person - background) weight difference of the classifier, which gives a
 * 3x3 "where is the person" heat map.  The box is the first/second moment of
 * that heat map, so it has sub-cell accuracy despite the tiny grid.
 */
#ifndef __PD_INFER_H
#define __PD_INFER_H

#include <stdint.h>
#include "pd_model_data.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PD_CAM_W        PD_FEATURE_W
#define PD_CAM_H        PD_FEATURE_H
#define PD_CAM_CELLS    (PD_CAM_W * PD_CAM_H)

typedef struct
{
    int8_t   logit[2];          /* raw classifier output, [0]=background [1]=person */
    float    score;             /* sigmoid(person - background), 0.0 .. 1.0        */
    uint8_t  person;            /* score >= threshold                              */
    uint8_t  box_valid;         /* bounding box below is meaningful                */
    int16_t  x, y, w, h;        /* bounding box in 96x96 input coordinates         */
    uint8_t  cam[PD_CAM_CELLS]; /* normalised heat map 0..255 (debug / overlay)    */
    uint32_t cycles;            /* CPU cycles spent inside pd_run()                */
    uint32_t us;                /* the same, converted to microseconds             */
} pd_result_t;

/**
 * @brief  One-time setup: enables the cycle counter and checks that the static
 *         arena is large enough for every layer of the compiled-in model.
 * @return RT-style 0 on success, negative on a sizing problem.
 */
int pd_init(void);

/** @brief Pointer to the 96*96 int8 input tensor; fill it before pd_run(). */
int8_t *pd_input(void);

/** @brief Run the full network on the current input tensor. 0 on success. */
int pd_run(pd_result_t *res);

/** @brief Detection threshold on the sigmoid score, default 0.60f. */
void  pd_set_threshold(float th);
float pd_get_threshold(void);

/**
 * @brief  RGB565 -> luma -> int8 with a 2x2 box filter.
 *
 * Reads a (2*PD_INPUT_W) x (2*PD_INPUT_H) window, i.e. 192x192, starting at
 * @p src and using @p src_stride pixels per row, and writes PD_INPUT_W *
 * PD_INPUT_H int8 samples.  Quantisation is the canonical Visual Wake Words
 * mapping q = luma - 128 (input scale 1/127.5, zero point -1).
 *
 * @param src         top-left pixel of the 192x192 window, RGB565
 * @param src_stride  row pitch of the source frame, in pixels
 * @param dst         96*96 destination tensor (may be pd_input())
 * @param luma_out    optional 96*96 uint8 luma copy, NULL if not needed
 */
void pd_preprocess_rgb565(const uint16_t *src, uint32_t src_stride,
                          int8_t *dst, uint8_t *luma_out);

#ifdef __cplusplus
}
#endif

#endif /* __PD_INFER_H */
