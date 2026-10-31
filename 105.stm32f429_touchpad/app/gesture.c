/**
 ******************************************************************************
 * @file    gesture.c
 * @brief   Contact samples -> UI events (strokes, taps, scroll, pan).
 *
 *  Design notes
 *  ------------
 *  - One sample per touch poll (~10 ms).  Each sample carries the track id the
 *    controller assigned to every contact; a small live table keeps the
 *    position per id so a finger can be followed individually.
 *  - The PRIMARY finger - the live id with the oldest birth stamp - drives the
 *    stroke.  This is what makes multi-touch stable: adding a second finger
 *    used to change the buffer order and the ink jumped to the new one.
 *  - Scroll / pan accumulate the *per-id* delta of the fingers that were
 *    present in both frames.  A newly landed finger has no previous position,
 *    so it contributes nothing; the centroid would have been dragged towards
 *    the new finger and produced a phantom notch.
 *  - A tap is "contact shorter than GESTURE_TAP_MS that never moved further
 *    than GESTURE_TAP_MOVE_PX".  Movement is accumulated as an absolute sum,
 *    so a slow drag that returns to its start point cannot be mistaken for a
 *    tap.
 *  - Once a second finger appears during a contact, the whole contact becomes a
 *    multi-finger gesture: the open stroke is closed (RELEASE) and no further
 *    ink is produced until every finger is lifted.  Without that latch a scroll
 *    would leave a diagonal smear across the drawing area.
 *  - Scroll / pan accumulate finger travel in pixels and convert one notch per
 *    GESTURE_SCROLL_NOTCH_PX / GESTURE_PAN_NOTCH_PX, so the notch count does
 *    not depend on how the movement was split across samples.
 ******************************************************************************
 */
#include <string.h>

#include "gesture.h"

/* ---- Tuning -------------------------------------------------------------- */
#define GESTURE_TAP_MS              260U    /* longest contact still a tap   */
#define GESTURE_TAP_MOVE_PX         14      /* movement that cancels a tap   */
#define GESTURE_SCROLL_NOTCH_PX     24      /* finger px per scroll notch    */
#define GESTURE_PAN_NOTCH_PX        30      /* finger px per pan notch       */
#define GESTURE_MOVE_MIN_PX         1       /* per-sample ink threshold      */

/* Event queue depth.  The UI drains it every 5 ms, so 32 is minutes of slack. */
#define GESTURE_QUEUE_LEN           32U

/* ---- State --------------------------------------------------------------- */
typedef enum
{
    GESTURE_MODE_IDLE = 0,  /* no contact                                   */
    GESTURE_MODE_DRAW,      /* one finger, ink is being produced            */
    GESTURE_MODE_MULTI      /* two or more fingers seen, no ink until lift  */
} gesture_mode_t;

/** One live contact: the controller's id, its position and when it appeared. */
typedef struct
{
    uint8_t  id;
    uint8_t  seen;      /* present in the frame being processed             */
    uint8_t  matched;   /* also present in the previous frame               */
    uint16_t born;      /* birth stamp, smaller = older finger              */
    int32_t  x;
    int32_t  y;
    int32_t  dx;        /* movement since the previous frame                */
    int32_t  dy;
} gesture_track_t;

static struct
{
    gesture_mode_t mode;
    uint8_t  prev_n;            /* contacts in the previous sample          */
    uint8_t  max_n;             /* most contacts seen during this contact   */
    uint32_t start_ms;
    int32_t  last_x;            /* previous primary position (ink + taps)   */
    int32_t  last_y;
    int32_t  move_px;           /* absolute travel, for tap detection       */
    int32_t  scroll_acc;        /* px not yet converted into notches        */
    int32_t  pan_acc;
    int32_t  last_tap_x;        /* position of the most recent gesture      */
    int32_t  last_tap_y;
    gesture_status_t st;

    /* Live id table, rebuilt on every sample. */
    gesture_track_t track[GESTURE_MAX_POINTS];
    uint8_t  track_n;
    uint16_t birth_seq;
} s;

static gesture_evt_t s_queue[GESTURE_QUEUE_LEN];
static uint8_t s_head;          /* write index                              */
static uint8_t s_tail;          /* read  index                              */

/* -------------------------------------------------------------------------- */
/* Queue                                                                      */
/* -------------------------------------------------------------------------- */
static void push_event(uint8_t type, int32_t x, int32_t y, int32_t d)
{
    uint8_t next = (uint8_t)((s_head + 1U) % GESTURE_QUEUE_LEN);

    if (next == s_tail)
    {
        /* Consumer is not keeping up (or nobody is draining at all).  Drop the
         * newest event rather than overwrite an older one: the UI stays
         * consistent, it only loses detail. */
        s.st.dropped++;
        return;
    }

    s_queue[s_head].type = type;
    s_queue[s_head].x    = (int16_t)x;
    s_queue[s_head].y    = (int16_t)y;
    s_queue[s_head].d    = (int16_t)d;
    s_head = next;
}

int gesture_poll(gesture_evt_t *out)
{
    if ((out == NULL) || (s_tail == s_head))
    {
        return 0;
    }

    *out   = s_queue[s_tail];
    s_tail = (uint8_t)((s_tail + 1U) % GESTURE_QUEUE_LEN);
    return 1;
}

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */
static int32_t iabs32(int32_t v)
{
    return (v < 0) ? -v : v;
}

/** Distance travelled from the last centroid, in whole pixels. */
static int32_t step_px(int32_t dx, int32_t dy)
{
    return iabs32(dx) + iabs32(dy);
}

/** One scroll notch per GESTURE_SCROLL_NOTCH_PX of vertical finger travel. */
static void emit_scroll_notches(void)
{
    while (s.scroll_acc >= GESTURE_SCROLL_NOTCH_PX)
    {
        s.scroll_acc -= GESTURE_SCROLL_NOTCH_PX;
        s.st.scrolls++;
        push_event(GESTURE_EVT_SCROLL, s.last_tap_x, s.last_tap_y, 1);
    }
    while (s.scroll_acc <= -GESTURE_SCROLL_NOTCH_PX)
    {
        s.scroll_acc += GESTURE_SCROLL_NOTCH_PX;
        s.st.scrolls++;
        push_event(GESTURE_EVT_SCROLL, s.last_tap_x, s.last_tap_y, -1);
    }
}

static void emit_pan_notches(void)
{
    while (s.pan_acc >= GESTURE_PAN_NOTCH_PX)
    {
        s.pan_acc -= GESTURE_PAN_NOTCH_PX;
        s.st.pans++;
        push_event(GESTURE_EVT_PAN, s.last_tap_x, s.last_tap_y, 1);
    }
    while (s.pan_acc <= -GESTURE_PAN_NOTCH_PX)
    {
        s.pan_acc += GESTURE_PAN_NOTCH_PX;
        s.st.pans++;
        push_event(GESTURE_EVT_PAN, s.last_tap_x, s.last_tap_y, -1);
    }
}

/* -------------------------------------------------------------------------- */
/* Contact (track id) table                                                   */
/* -------------------------------------------------------------------------- */

/** Insert a contact that was not in the previous sample. */
static void track_insert(const gesture_point_t *p)
{
    gesture_track_t *t;
    uint8_t i;

    if (s.track_n >= (uint8_t)GESTURE_MAX_POINTS)
    {
        /* Table full: reuse the slot of a finger that is gone in this frame -
         * it is about to be compacted away anyway.  If every slot is live,
         * something is off (the controller reported more ids than its
         * configured point count), so drop the contact instead of corrupting
         * the table. */
        for (i = 0U; i < s.track_n; i++)
        {
            if (s.track[i].seen == 0U)
            {
                break;
            }
        }
        if (i >= s.track_n)
        {
            return;
        }
        t = &s.track[i];
    }
    else
    {
        t = &s.track[s.track_n];
        s.track_n++;
    }

    s.birth_seq++;
    t->id   = p->id;
    t->born = s.birth_seq;
    t->seen = 1U;
    t->x    = (int32_t)p->x;
    t->y    = (int32_t)p->y;
}

/**
 * @brief  Refresh the live id table from one sample.
 * @param  prim_x/prim_y  position of the primary (oldest) contact, 0 when the
 *                        sample is empty
 * @param  mean_dx/dy     average movement of the two OLDEST contacts that
 *                        existed in both this and the previous sample
 *                        (0 when neither did)
 */
static void tracks_update(const gesture_point_t *pts, uint8_t n,
                          int32_t *prim_x, int32_t *prim_y,
                          int32_t *mean_dx, int32_t *mean_dy)
{
    int32_t d1x = 0;
    int32_t d1y = 0;
    int32_t d2x = 0;
    int32_t d2y = 0;
    uint16_t b1 = 0xFFFFU;      /* birth stamp of the oldest matched finger  */
    uint16_t b2 = 0xFFFFU;      /* ... and of the second oldest              */
    uint8_t i;
    uint8_t best;

    for (i = 0U; i < s.track_n; i++)
    {
        s.track[i].seen    = 0U;
        s.track[i].matched = 0U;
        s.track[i].dx      = 0;
        s.track[i].dy      = 0;
    }

    for (i = 0U; i < n; i++)
    {
        gesture_track_t *t = NULL;
        uint8_t k;

        /* Same track id and not already claimed by another contact of this
         * frame -> it is the same physical finger as last time. */
        for (k = 0U; k < s.track_n; k++)
        {
            if ((s.track[k].seen == 0U) && (s.track[k].id == pts[i].id))
            {
                t = &s.track[k];
                break;
            }
        }

        if (t == NULL)
        {
            track_insert(&pts[i]);
            continue;
        }

        t->dx      = (int32_t)pts[i].x - t->x;
        t->dy      = (int32_t)pts[i].y - t->y;
        t->x       = (int32_t)pts[i].x;
        t->y       = (int32_t)pts[i].y;
        t->seen    = 1U;
        t->matched = 1U;
    }

    /* Drop the fingers that are no longer reported. */
    for (i = 0U; i < s.track_n; i++)
    {
        if (s.track[i].seen == 0U)
        {
            s.track[i] = s.track[s.track_n - 1U];
            s.track_n--;
            i--;                /* re-check the slot we just moved down */
        }
    }

    /* Primary = oldest live contact (smallest birth stamp). */
    *prim_x = 0;
    *prim_y = 0;
    best    = 0U;
    for (i = 1U; i < s.track_n; i++)
    {
        if (s.track[i].born < s.track[best].born)
        {
            best = i;
        }
    }
    if (s.track_n > 0U)
    {
        *prim_x = s.track[best].x;
        *prim_y = s.track[best].y;
    }

    /* Scroll / pan reference = mean movement of the two oldest fingers that
     * were already down in the previous frame.
     *
     * Deliberately NOT the mean of every matched finger: a third finger
     * resting on the panel would otherwise dilute the two-finger motion (three
     * matched fingers, one still -> only two thirds of the travel is counted)
     * and the scroll would feel sticky.  Deliberately also not the centroid of
     * the frame: a finger that just landed has no previous position and would
     * drag the reference towards itself, which is a phantom notch. */
    for (i = 0U; i < s.track_n; i++)
    {
        if (s.track[i].matched == 0U)
        {
            continue;
        }
        if (s.track[i].born < b1)
        {
            b2  = b1;  d2x = d1x;  d2y = d1y;
            b1  = s.track[i].born;
            d1x = s.track[i].dx;
            d1y = s.track[i].dy;
        }
        else if (s.track[i].born < b2)
        {
            b2  = s.track[i].born;
            d2x = s.track[i].dx;
            d2y = s.track[i].dy;
        }
    }

    if (b1 == 0xFFFFU)
    {
        *mean_dx = 0;
        *mean_dy = 0;
    }
    else if (b2 == 0xFFFFU)
    {
        *mean_dx = d1x;
        *mean_dy = d1y;
    }
    else
    {
        *mean_dx = (d1x + d2x) / 2;
        *mean_dy = (d1y + d2y) / 2;
    }
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */
void gesture_init(void)
{
    memset(&s, 0, sizeof(s));
    s.mode  = GESTURE_MODE_IDLE;
    s_head  = 0U;
    s_tail  = 0U;
}

void gesture_feed(uint32_t now_ms, const gesture_point_t *pts, uint8_t n)
{
    int32_t cx = 0;
    int32_t cy = 0;
    int32_t px = 0;
    int32_t py = 0;
    int32_t mdx = 0;
    int32_t mdy = 0;
    uint8_t i;

    if ((pts == NULL) || (n == 0U))
    {
        n = 0U;
    }
    if (n > GESTURE_MAX_POINTS)
    {
        n = GESTURE_MAX_POINTS;
    }

    for (i = 0U; i < n; i++)
    {
        cx += (int32_t)pts[i].x;
        cy += (int32_t)pts[i].y;
    }
    if (n > 0U)
    {
        cx /= (int32_t)n;
        cy /= (int32_t)n;
    }

    tracks_update(pts, n, &px, &py, &mdx, &mdy);

    s.st.fingers = n;
    s.st.active  = (n > 0U) ? 1U : 0U;

    /* ---- Contact starts ------------------------------------------------- */
    if ((n > 0U) && (s.prev_n == 0U))
    {
        s.start_ms   = now_ms;
        s.max_n      = n;
        s.move_px    = 0;
        s.scroll_acc = 0;
        s.pan_acc    = 0;
        s.last_x     = px;
        s.last_y     = py;

        if (n == 1U)
        {
            s.mode = GESTURE_MODE_DRAW;
            s.st.presses++;
            push_event(GESTURE_EVT_PRESS, px, py, 0);
        }
        else
        {
            s.mode = GESTURE_MODE_MULTI;
        }
    }
    /* ---- Extra finger: close the stroke and stay in gesture mode -------- */
    else if ((n >= 2U) && (s.mode == GESTURE_MODE_DRAW))
    {
        s.mode = GESTURE_MODE_MULTI;
        push_event(GESTURE_EVT_RELEASE, s.last_x, s.last_y, 0);
        s.last_x = px;
        s.last_y = py;
    }
    else if ((n >= 2U) && (s.mode == GESTURE_MODE_MULTI))
    {
        /* Scroll / pan from the per-finger deltas, not from the centroid: a
         * finger that just landed has no previous position and must not pull
         * the reference towards itself.  d = +1 means "fingers moved down /
         * right". */
        s.scroll_acc += mdy;
        s.pan_acc    += mdx;
        s.last_x      = px;
        s.last_y      = py;
        s.last_tap_x  = cx;
        s.last_tap_y  = cy;

        emit_scroll_notches();
        emit_pan_notches();
    }
    /* ---- Single finger already down ------------------------------------- */
    else if ((n == 1U) && (s.mode == GESTURE_MODE_DRAW))
    {
        int32_t dx = px - s.last_x;
        int32_t dy = py - s.last_y;

        s.move_px += step_px(dx, dy);

        if ((step_px(dx, dy) >= GESTURE_MOVE_MIN_PX))
        {
            push_event(GESTURE_EVT_MOVE, px, py, 0);
            s.last_x = px;
            s.last_y = py;
        }
    }
    /* ---- Contact ends --------------------------------------------------- */
    else if ((n == 0U) && (s.prev_n > 0U))
    {
        uint32_t duration = now_ms - s.start_ms;

        if ((duration <= GESTURE_TAP_MS) && (s.move_px < GESTURE_TAP_MOVE_PX))
        {
            s.last_tap_x = s.last_x;
            s.last_tap_y = s.last_y;

            if (s.max_n == 1U)
            {
                s.st.taps++;
                push_event(GESTURE_EVT_TAP, s.last_x, s.last_y, 0);
            }
            else
            {
                s.st.multi_taps++;
                push_event((s.max_n == 2U) ? GESTURE_EVT_TAP2 : GESTURE_EVT_TAP3,
                           s.last_x, s.last_y, 0);
            }
        }
        else if (s.mode == GESTURE_MODE_DRAW)
        {
            push_event(GESTURE_EVT_RELEASE, s.last_x, s.last_y, 0);
        }

        s.mode = GESTURE_MODE_IDLE;
        s.track_n = 0U;     /* every finger is up: no id may survive */
    }

    s.prev_n = n;
}

void gesture_get_status(gesture_status_t *out)
{
    if (out != NULL)
    {
        *out = s.st;
    }
}
