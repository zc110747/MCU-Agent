/* ---------------------------------------------------------------------------
 * image_motion.c - SAD block matching on a down-sampled luma plane.
 * -------------------------------------------------------------------------*/

#include "image_motion.h"

#define GRAY_W          IMAGE_MOTION_GRAY_W
#define GRAY_H          IMAGE_MOTION_GRAY_H
#define BLOCK           IMAGE_MOTION_BLOCK
#define MAX_SHIFT       IMAGE_MOTION_MAX_SHIFT
#define REF_W           IMAGE_MOTION_REF_W
#define REF_H           IMAGE_MOTION_REF_H

#define BLOCK_PIXELS    (BLOCK * BLOCK)

/* Working buffers. 4.8 KB luma + 1.6 KB reference; both are touched by the CPU
 * only, so plain .bss (DTCM) is the fastest and cheapest place for them. */
static uint8_t s_gray[GRAY_W * GRAY_H];
static uint8_t s_ref[REF_W * REF_H];

static bool     s_have_ref;
static uint16_t s_block_x;      /* top-left of the match block inside s_gray */
static uint16_t s_block_y;

/* ------------------------------------------------------------------------ */
static void compute_block_origin(void)
{
    s_block_x = (uint16_t) ((GRAY_W - BLOCK) / 2u);
    s_block_y = (uint16_t) ((GRAY_H - BLOCK) / 2u);
}

void image_motion_reset(void)
{
    s_have_ref = false;
    compute_block_origin();
}

/* ---------------------------------------------------------------------------
 * Luma extraction. Both paths average DOWNSCALE x DOWNSCALE source pixels,
 * which also low-pass filters the sensor noise that would otherwise bias the
 * SAD towards whichever candidate happened to align with a noise grain.
 * -------------------------------------------------------------------------*/
static void extract_luma_yuyv(const uint8_t *frame, uint16_t width)
{
    for (uint32_t gy = 0u; gy < GRAY_H; gy++)
    {
        const uint8_t *row0 = frame + ((gy * IMAGE_MOTION_DOWNSCALE) * width * 2u);
        const uint8_t *row1 = row0 + (width * 2u);
        uint8_t       *dst  = &s_gray[gy * GRAY_W];

        for (uint32_t gx = 0u; gx < GRAY_W; gx++)
        {
            uint32_t x = gx * IMAGE_MOTION_DOWNSCALE;
            uint32_t sum;

            /* YUYV: Y0 U Y1 V - luma lives at even byte offsets. */
            sum = (uint32_t) row0[(x * 2u)] + (uint32_t) row0[(x * 2u) + 2u] +
                  (uint32_t) row1[(x * 2u)] + (uint32_t) row1[(x * 2u) + 2u];
            dst[gx] = (uint8_t) (sum / 4u);
        }
    }
}

static void extract_luma_gray(const uint8_t *gray, uint16_t width)
{
    for (uint32_t gy = 0u; gy < GRAY_H; gy++)
    {
        const uint8_t *row0 = gray + ((gy * IMAGE_MOTION_DOWNSCALE) * width);
        const uint8_t *row1 = row0 + width;
        uint8_t       *dst  = &s_gray[gy * GRAY_W];

        for (uint32_t gx = 0u; gx < GRAY_W; gx++)
        {
            uint32_t x = gx * IMAGE_MOTION_DOWNSCALE;
            uint32_t sum = (uint32_t) row0[x] + (uint32_t) row0[x + 1u] +
                           (uint32_t) row1[x] + (uint32_t) row1[x + 1u];

            dst[gx] = (uint8_t) (sum / 4u);
        }
    }
}

/* ---------------------------------------------------------------------------
 * Reference window: the block plus MAX_SHIFT pixels of context on every side,
 * taken from the *previous* frame.
 * -------------------------------------------------------------------------*/
static void save_reference(void)
{
    uint32_t origin_x = (uint32_t) s_block_x - MAX_SHIFT;
    uint32_t origin_y = (uint32_t) s_block_y - MAX_SHIFT;

    for (uint32_t row = 0u; row < REF_H; row++)
    {
        const uint8_t *src = &s_gray[((origin_y + row) * GRAY_W) + origin_x];
        uint8_t       *dst = &s_ref[row * REF_W];

        for (uint32_t col = 0u; col < REF_W; col++)
        {
            dst[col] = src[col];
        }
    }

    s_have_ref = true;
}

/* Min/max of the match block. A flat block cannot be matched: every candidate
 * position scores the same, so the argmin is decided by noise. */
static uint32_t block_contrast(void)
{
    uint8_t low  = 255u;
    uint8_t high = 0u;

    for (uint32_t row = 0u; row < BLOCK; row++)
    {
        const uint8_t *src = &s_gray[((s_block_y + row) * GRAY_W) + s_block_x];

        for (uint32_t col = 0u; col < BLOCK; col++)
        {
            uint8_t value = src[col];

            if (value < low)
            {
                low = value;
            }
            if (value > high)
            {
                high = value;
            }
        }
    }

    return (uint32_t) (high - low);
}

/* ---------------------------------------------------------------------------
 * Search
 * -------------------------------------------------------------------------*/
static uint32_t sad_at(uint32_t offset_x, uint32_t offset_y, uint32_t cutoff)
{
    uint32_t sad = 0u;

    for (uint32_t row = 0u; row < BLOCK; row++)
    {
        const uint8_t *ref = &s_ref[((offset_y + row) * REF_W) + offset_x];
        const uint8_t *cur = &s_gray[((s_block_y + row) * GRAY_W) + s_block_x];

        for (uint32_t col = 0u; col < BLOCK; col++)
        {
            uint8_t a = ref[col];
            uint8_t b = cur[col];

            sad += (a > b) ? (uint32_t) (a - b) : (uint32_t) (b - a);
        }

        /* Row-level early exit. Once a candidate is already worse than the
         * best so far it can never win, so the remaining rows are pure waste.
         * On real desk texture this removes ~70 % of the work. */
        if (sad >= cutoff)
        {
            return sad;
        }
    }

    return sad;
}

/* ------------------------------------------------------------------------ */
static bool run_matching(mouse_motion_t *motion)
{
    uint32_t best_sad   = 0xFFFFFFFFu;
    uint32_t best_x     = MAX_SHIFT;
    uint32_t best_y     = MAX_SHIFT;
    uint32_t contrast;

    contrast = block_contrast();
    motion->texture = (uint8_t) contrast;

    if (contrast < IMAGE_MOTION_MIN_CONTRAST)
    {
        /* Blank surface or a covered lens. Report it instead of pretending the
         * cursor should sit still - the filter layer treats invalid frames as
         * "no data", not as "zero motion". */
        motion->valid   = 0u;
        motion->quality = 0u;
        return false;
    }

    for (uint32_t offset_y = 0u; offset_y <= (2u * MAX_SHIFT); offset_y++)
    {
        for (uint32_t offset_x = 0u; offset_x <= (2u * MAX_SHIFT); offset_x++)
        {
            uint32_t sad = sad_at(offset_x, offset_y, best_sad);

            if (sad < best_sad)
            {
                best_sad = sad;
                best_x   = offset_x;
                best_y   = offset_y;
            }
        }
    }

    {
        uint32_t limit = BLOCK_PIXELS * IMAGE_MOTION_MAX_SAD_PER_PX;

        if (best_sad > limit)
        {
            /* A minimum exists but it is not a real match - typically motion
             * faster than MAX_SHIFT, or a scene change. Refusing to guess is
             * what keeps a fast flick from producing an inverted jolt. */
            motion->valid   = 0u;
            motion->quality = 0u;
            return false;
        }

        motion->quality = (uint16_t) (1000u -
            ((best_sad * 1000u) / limit));
    }

    /* Sign convention: s_ref was captured one frame ago. A candidate at
     * (block + offset - MAX_SHIFT) means the pattern that is now in the middle
     * of the frame used to sit there, i.e. the image shifted by
     * (MAX_SHIFT - offset) and the mouse moved by the negation of that. */
    motion->delta_x = (int16_t) ((int32_t) best_x - (int32_t) MAX_SHIFT);
    motion->delta_y = (int16_t) ((int32_t) best_y - (int32_t) MAX_SHIFT);

#if IMAGE_MOTION_INVERT_X
    motion->delta_x = (int16_t) -motion->delta_x;
#endif
#if IMAGE_MOTION_INVERT_Y
    motion->delta_y = (int16_t) -motion->delta_y;
#endif

    motion->valid = 1u;
    return true;
}

/* ------------------------------------------------------------------------ */
static bool finish_frame(mouse_motion_t *motion)
{
    bool matched;

    if (motion == 0)
    {
        return false;
    }

    motion->delta_x = 0;
    motion->delta_y = 0;
    motion->quality = 0u;
    motion->valid   = 0u;
    motion->texture = 0u;

    if (s_have_ref == false)
    {
        /* First frame after a reset: nothing to compare against yet. */
        save_reference();
        return false;
    }

    matched = run_matching(motion);

    /* The reference always advances, even when the match was rejected. Keeping
     * a stale reference would make the next frame compare against ever older
     * content, and a single bad frame would poison the rest of the session. */
    save_reference();

    return matched;
}

bool image_motion_process_yuyv(const uint8_t *frame, uint16_t width, uint16_t height,
                               mouse_motion_t *motion)
{
    if ((frame == 0) ||
        (width != IMAGE_MOTION_FRAME_W) || (height != IMAGE_MOTION_FRAME_H))
    {
        return false;
    }

    extract_luma_yuyv(frame, width);
    return finish_frame(motion);
}

bool image_motion_process_gray(const uint8_t *gray, uint16_t width, uint16_t height,
                               mouse_motion_t *motion)
{
    if ((gray == 0) ||
        (width != IMAGE_MOTION_FRAME_W) || (height != IMAGE_MOTION_FRAME_H))
    {
        return false;
    }

    extract_luma_gray(gray, width);
    return finish_frame(motion);
}
