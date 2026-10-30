/* ---------------------------------------------------------------------------
 * motion_filter.h - turns raw per-frame displacements into mouse counts.
 *
 * Pipeline, in order:
 *
 *   invalid frame  -> dropped, counted (never reported as "zero motion")
 *   dead band      -> sub-threshold motion accumulates instead of being lost
 *   sensitivity    -> rational scale, integer only (num/den)
 *   acceleration   -> optional, off by default
 *   smoothing      -> optional first-order IIR, off by default
 *   sanity clamp   -> a single absurd frame cannot fling the cursor
 *
 * Why a dead band instead of a "discard small movements" rule: a threshold
 * that throws the delta away silently destroys slow, deliberate motion - the
 * exact motion a user makes when aiming at a 16-pixel icon. Accumulating and
 * releasing once the threshold is crossed suppresses the +-1 resting noise of
 * the SAD matcher without losing anything.
 *
 * Why smoothing is off by default: every IIR stage adds latency, and latency
 * on a pointing device is felt immediately. It exists for the noisy-sensor
 * case, not for the normal one.
 * -------------------------------------------------------------------------*/

#ifndef MOTION_FILTER_H_
#define MOTION_FILTER_H_

#include <stdint.h>
#include <stdbool.h>
#include "image_motion.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint16_t sensitivity_num;       /* counts per source pixel, numerator  */
    uint16_t sensitivity_den;       /* denominator (never 0)               */
    uint16_t jitter_threshold;      /* dead band in source pixels, 1 = off */
    uint16_t accel_threshold;       /* |delta| above which accel applies, 0 = off */
    uint16_t accel_num;
    uint16_t accel_den;
    uint16_t smoothing;             /* IIR weight 0..15, 0 = off           */
    uint16_t max_counts_per_frame;  /* sanity clamp, counts                */
} motion_filter_config_t;

typedef struct
{
    uint32_t invalid_frames;
    uint32_t idle_frames;           /* valid but consumed by the dead band  */
    uint32_t clamped_frames;
    uint32_t released_events;       /* dead band released something         */
} motion_filter_stats_t;

void motion_filter_init(void);

/* Clear the dead-band and IIR state. Call when the video pipeline restarts so
 * motion measured before the gap does not leak into the new session. */
void motion_filter_reset(void);

/* Returns true when `delta_x`/`delta_y` (counts) should be forwarded to the
 * mouse core. Returns false for invalid frames and for frames fully absorbed
 * by the dead band; in that case the out parameters are untouched. */
bool motion_filter_apply(const mouse_motion_t *motion, int32_t *delta_x, int32_t *delta_y);

/* Runtime tuning. `den` must be non-zero; num/den is applied as-is, so
 * sensitivity_num = 4, sensitivity_den = 1 means 4 counts per source pixel. */
bool motion_filter_set_sensitivity(uint16_t num, uint16_t den);

const motion_filter_config_t *motion_filter_get_config(void);
void motion_filter_get_stats(motion_filter_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* MOTION_FILTER_H_ */
