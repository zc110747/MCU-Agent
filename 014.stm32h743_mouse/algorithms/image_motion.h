/* ---------------------------------------------------------------------------
 * image_motion.h - relative displacement from consecutive frames.
 *
 * Chosen algorithm: block matching by SAD (sum of absolute differences) on a
 * down-sampled luma plane. The alternatives were evaluated against this
 * project's constraints (see docs/architecture.md for the full table):
 *
 *   Template matching (NCC)   - multiplies and a divide per pixel, ~8x the
 *                               cycles of SAD for the same accuracy on a
 *                               low-contrast desk surface.
 *   Lucas-Kanade optical flow - sub-pixel accurate, but needs float or a well
 *                               tuned Q-format, an iterative solve, and it
 *                               falls apart on the same low-texture surfaces
 *                               a mouse spends its life on.
 *   SAD block matching        - integer only, one accumulator per candidate,
 *                               trivially bounded, and the quality metric
 *                               falls out of the result for free.
 *
 * SAD is what commercial optical mice used before dedicated DSPs, and it is
 * the right answer at 400 MHz with 6 KB of scratch RAM.
 *
 * Coordinate system (matches HID and the user's spec):
 *     +x = right,  +y = down,  unit = pixel at the down-sampled scale.
 * The lens is bolted to the mouse, so the surface image moves OPPOSITE to the
 * mouse. The sign inversion is applied here, once, with compile-time switches
 * for a sensor mounted upside down.
 * -------------------------------------------------------------------------*/

#ifndef IMAGE_MOTION_H_
#define IMAGE_MOTION_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Geometry
 *
 * The frame size is a compile-time constant: it lets the working buffers be
 * sized exactly (6.4 KB instead of a worst-case 19 KB) and turns a camera
 * reconfiguration into a deliberate edit rather than a silent mismatch.
 * -------------------------------------------------------------------------*/
#define IMAGE_MOTION_FRAME_W      160u
#define IMAGE_MOTION_FRAME_H      120u

/* Luma is averaged over DOWNSCALE x DOWNSCALE blocks. 2 halves the SAD work
 * per axis (4x total) and costs half a pixel of resolution per count - the
 * sensitivity setting absorbs that. */
#define IMAGE_MOTION_DOWNSCALE    2u

#define IMAGE_MOTION_GRAY_W       (IMAGE_MOTION_FRAME_W / IMAGE_MOTION_DOWNSCALE)
#define IMAGE_MOTION_GRAY_H       (IMAGE_MOTION_FRAME_H / IMAGE_MOTION_DOWNSCALE)

/* Match block, in down-sampled pixels. 24x24 keeps 4-6 usable texture features
 * on a normal desk while staying small enough that perspective distortion
 * across the block does not dominate the SAD. */
#define IMAGE_MOTION_BLOCK        24u

/* +-8 down-sampled pixels per frame = +-16 source pixels. At 30 fps that is a
 * 1440 px/s ceiling before the match becomes ambiguous; anything faster is
 * reported as invalid rather than as a wrong direction. */
#define IMAGE_MOTION_MAX_SHIFT    8

#define IMAGE_MOTION_REF_W        (IMAGE_MOTION_BLOCK + (2u * IMAGE_MOTION_MAX_SHIFT))
#define IMAGE_MOTION_REF_H        (IMAGE_MOTION_BLOCK + (2u * IMAGE_MOTION_MAX_SHIFT))

/* Flip the reported axis. Set to 1 if the sensor ends up mounted rotated. */
#define IMAGE_MOTION_INVERT_X     0
#define IMAGE_MOTION_INVERT_Y     0

/* A block whose luma span is below this is "flat" - a blank sheet of paper or
 * a finger over the lens. Matching on it produces confident garbage, so it is
 * rejected instead. */
#define IMAGE_MOTION_MIN_CONTRAST 10u

/* SAD above this (per pixel, 0..255) means the match is not believable even
 * though a minimum exists. */
#define IMAGE_MOTION_MAX_SAD_PER_PX 24u

typedef struct
{
    int16_t  delta_x;    /* + = right, in down-sampled pixels              */
    int16_t  delta_y;    /* + = down                                       */
    uint16_t quality;    /* 0..1000, higher is a better match              */
    uint8_t  valid;      /* 0 = do not use delta_x / delta_y               */
    uint8_t  texture;    /* 0..255, luma span of the block (diagnostic)    */
} mouse_motion_t;

/* Drop the reference frame. The next process() call re-arms it and reports
 * valid = 0 - there is nothing to compare against yet. */
void image_motion_reset(void);

/* Feed one YUYV (a.k.a. YUY2) frame. `width`/`height` must equal
 * IMAGE_MOTION_FRAME_W/H; anything else is rejected rather than cropped.
 * Returns true when `motion` holds a valid displacement. */
bool image_motion_process_yuyv(const uint8_t *frame, uint16_t width, uint16_t height,
                               mouse_motion_t *motion);

/* Same, for an 8-bit luma plane already at full resolution. Used by the unit
 * tests and by any future sensor that hands over luma directly. */
bool image_motion_process_gray(const uint8_t *gray, uint16_t width, uint16_t height,
                               mouse_motion_t *motion);

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_MOTION_H_ */
