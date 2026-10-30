/* ---------------------------------------------------------------------------
 * motion_filter.c - see motion_filter.h for the pipeline order and rationale.
 * -------------------------------------------------------------------------*/

#include "motion_filter.h"

/* Defaults. A 160x120 frame at 2x downscale is 80x60; a mouse moved 1 mm on a
 * normal desk covers roughly 3-4 down-sampled pixels, so 4 counts per pixel
 * gives a pointer speed in the usual range without any host-side curve. */
#define DEFAULT_SENS_NUM        4u
#define DEFAULT_SENS_DEN        1u
#define DEFAULT_JITTER          2u
#define DEFAULT_MAX_COUNTS      200u

/* Dead-band accumulator ceiling, in source pixels. Without it a permanently
 * invalid scene (lens covered, moving blank paper) could build a huge backlog
 * that discharges the moment texture returns. */
#define DEADBAND_LIMIT          16

typedef struct
{
    int32_t accum_x;        /* dead-band accumulator, source pixels        */
    int32_t accum_y;
    int32_t smooth_x;       /* IIR state, counts                           */
    int32_t smooth_y;

    uint32_t invalid_frames;
    uint32_t idle_frames;
    uint32_t clamped_frames;
    uint32_t released_events;

    motion_filter_config_t cfg;
} motion_filter_state_t;

static motion_filter_state_t g_filter =
{
    .accum_x = 0,
    .accum_y = 0,
    .smooth_x = 0,
    .smooth_y = 0,
    .invalid_frames = 0u,
    .idle_frames = 0u,
    .clamped_frames = 0u,
    .released_events = 0u,
    .cfg =
    {
        .sensitivity_num = DEFAULT_SENS_NUM,
        .sensitivity_den = DEFAULT_SENS_DEN,
        .jitter_threshold = DEFAULT_JITTER,
        .accel_threshold = 0u,
        .accel_num = 1u,
        .accel_den = 1u,
        .smoothing = 0u,
        .max_counts_per_frame = DEFAULT_MAX_COUNTS
    }
};

/* ------------------------------------------------------------------------ */
static int32_t abs_i32(int32_t value)
{
    return (value < 0) ? -value : value;
}

static int32_t clamp_i32(int32_t value, int32_t limit)
{
    if (value > limit)
    {
        return limit;
    }
    if (value < -limit)
    {
        return -limit;
    }
    return value;
}

/* ------------------------------------------------------------------------ */
void motion_filter_init(void)
{
    g_filter.cfg.sensitivity_num      = DEFAULT_SENS_NUM;
    g_filter.cfg.sensitivity_den      = DEFAULT_SENS_DEN;
    g_filter.cfg.jitter_threshold     = DEFAULT_JITTER;
    g_filter.cfg.accel_threshold      = 0u;
    g_filter.cfg.accel_num            = 1u;
    g_filter.cfg.accel_den            = 1u;
    g_filter.cfg.smoothing            = 0u;
    g_filter.cfg.max_counts_per_frame = DEFAULT_MAX_COUNTS;

    motion_filter_reset();
}

void motion_filter_reset(void)
{
    g_filter.accum_x = 0;
    g_filter.accum_y = 0;
    g_filter.smooth_x = 0;
    g_filter.smooth_y = 0;

    g_filter.invalid_frames = 0u;
    g_filter.idle_frames    = 0u;
    g_filter.clamped_frames = 0u;
    g_filter.released_events = 0u;
}

bool motion_filter_set_sensitivity(uint16_t num, uint16_t den)
{
    if ((num == 0u) || (den == 0u))
    {
        return false;
    }

    g_filter.cfg.sensitivity_num = num;
    g_filter.cfg.sensitivity_den = den;
    return true;
}

/* ------------------------------------------------------------------------ */
static int32_t apply_dead_band(int32_t delta, int32_t *accumulator, bool *released)
{
    uint32_t threshold = g_filter.cfg.jitter_threshold;

    *accumulator += delta;
    *accumulator = clamp_i32(*accumulator, DEADBAND_LIMIT);

    if (threshold <= 1u)
    {
        /* Dead band disabled: hand the accumulator back verbatim. */
        int32_t value = *accumulator;

        *accumulator = 0;
        *released = (value != 0);
        return value;
    }

    if ((uint32_t) abs_i32(*accumulator) >= threshold)
    {
        int32_t value = *accumulator;

        *accumulator = 0;
        *released = true;
        return value;
    }

    return 0;
}

static int32_t scale(int32_t value, uint32_t num, uint32_t den)
{
    if (den == 0u)
    {
        return value;
    }

    /* Signed-safe rational scale. The magnitude is scaled as unsigned so a
     * negative value cannot lose its sign to integer division. */
    if (value < 0)
    {
        return -(int32_t) ((((uint32_t) (-value)) * num) / den);
    }

    return (int32_t) ((((uint32_t) value) * num) / den);
}

/* ------------------------------------------------------------------------ */
bool motion_filter_apply(const mouse_motion_t *motion, int32_t *delta_x, int32_t *delta_y)
{
    int32_t out_x;
    int32_t out_y;
    bool    released_x = false;
    bool    released_y = false;

    if ((motion == 0) || (delta_x == 0) || (delta_y == 0))
    {
        return false;
    }

    if (motion->valid == 0u)
    {
        /* Deliberately NOT treated as zero motion: a blocked or featureless
         * surface must not silently hold the cursor still as if the user had
         * stopped moving. The dead band is left untouched. */
        g_filter.invalid_frames++;
        return false;
    }

    out_x = apply_dead_band((int32_t) motion->delta_x, &g_filter.accum_x, &released_x);
    out_y = apply_dead_band((int32_t) motion->delta_y, &g_filter.accum_y, &released_y);

    if ((out_x == 0) && (out_y == 0))
    {
        g_filter.idle_frames++;
        return false;
    }

    if ((released_x == true) || (released_y == true))
    {
        g_filter.released_events++;
    }

    /* Sensitivity. Rational form keeps the fine steps available: 3/2 is a real
     * setting, not a rounding of 1. */
    out_x = scale(out_x, g_filter.cfg.sensitivity_num, g_filter.cfg.sensitivity_den);
    out_y = scale(out_y, g_filter.cfg.sensitivity_num, g_filter.cfg.sensitivity_den);

    /* Acceleration (off by default - see the header). */
    if (g_filter.cfg.accel_threshold != 0u)
    {
        int32_t threshold = (int32_t) g_filter.cfg.accel_threshold;

        if (abs_i32(out_x) > threshold)
        {
            out_x = scale(out_x, g_filter.cfg.accel_num, g_filter.cfg.accel_den);
        }
        if (abs_i32(out_y) > threshold)
        {
            out_y = scale(out_y, g_filter.cfg.accel_num, g_filter.cfg.accel_den);
        }
    }

    /* First-order IIR: out = (new * (16 - k) + previous * k) / 16, k in 1..15.
     * Weights are the remainder of the new sample, so k = 0 is "no smoothing"
     * and is skipped entirely rather than dividing by a zero remainder. */
    if ((g_filter.cfg.smoothing != 0u) && (g_filter.cfg.smoothing < 16u))
    {
        int32_t k = (int32_t) g_filter.cfg.smoothing;

        g_filter.smooth_x = ((out_x * (16 - k)) + (g_filter.smooth_x * k)) / 16;
        g_filter.smooth_y = ((out_y * (16 - k)) + (g_filter.smooth_y * k)) / 16;

        out_x = g_filter.smooth_x;
        out_y = g_filter.smooth_y;

        /* Rounded to zero: nothing to report, and the IIR state keeps the
         * sub-count remainder for the next frame. */
        if ((out_x == 0) && (out_y == 0))
        {
            g_filter.idle_frames++;
            return false;
        }
    }

    /* Sanity clamp. A single frame that claims a quarter of the screen is a
     * mis-match, not a user gesture. */
    {
        int32_t limit = (int32_t) g_filter.cfg.max_counts_per_frame;

        if ((abs_i32(out_x) > limit) || (abs_i32(out_y) > limit))
        {
            g_filter.clamped_frames++;
            out_x = clamp_i32(out_x, limit);
            out_y = clamp_i32(out_y, limit);
        }
    }

    *delta_x = out_x;
    *delta_y = out_y;
    return true;
}

/* ------------------------------------------------------------------------ */
const motion_filter_config_t *motion_filter_get_config(void)
{
    return &g_filter.cfg;
}

void motion_filter_get_stats(motion_filter_stats_t *stats)
{
    if (stats == 0)
    {
        return;
    }

    stats->invalid_frames  = g_filter.invalid_frames;
    stats->idle_frames     = g_filter.idle_frames;
    stats->clamped_frames  = g_filter.clamped_frames;
    stats->released_events = g_filter.released_events;
}
