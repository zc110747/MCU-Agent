/**
 ******************************************************************************
 * @file    gesture.c
 * @brief   Contact samples -> UI events (strokes, taps, scroll, pan).
 *
 *  Design notes
 *  ------------
 *  - One sample per touch poll (~10 ms).  The engine only looks at the
 *    *centroid* of the contacts plus their count, which is all these gestures
 *    need and far more stable than tracking individual point ids.
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

static struct
{
    gesture_mode_t mode;
    uint8_t  prev_n;            /* contacts in the previous sample          */
    uint8_t  max_n;             /* most contacts seen during this contact   */
    uint32_t start_ms;
    int32_t  last_x;            /* previous centroid                        */
    int32_t  last_y;
    int32_t  move_px;           /* absolute travel, for tap detection       */
    int32_t  scroll_acc;        /* px not yet converted into notches        */
    int32_t  pan_acc;
    int32_t  last_tap_x;        /* position of the most recent gesture      */
    int32_t  last_tap_y;
    gesture_status_t st;
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
        s.last_x     = cx;
        s.last_y     = cy;

        if (n == 1U)
        {
            s.mode = GESTURE_MODE_DRAW;
            s.st.presses++;
            push_event(GESTURE_EVT_PRESS, cx, cy, 0);
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
        s.last_x = cx;
        s.last_y = cy;
    }
    else if ((n >= 2U) && (s.mode == GESTURE_MODE_MULTI))
    {
        /* Scroll / pan: the centroid moves as both fingers move together.
         * d = +1 means "fingers moved down / right". */
        s.scroll_acc += (cy - s.last_y);
        s.pan_acc    += (cx - s.last_x);
        s.last_x = cx;
        s.last_y = cy;
        s.last_tap_x = cx;
        s.last_tap_y = cy;

        emit_scroll_notches();
        emit_pan_notches();
    }
    /* ---- Single finger already down ------------------------------------- */
    else if ((n == 1U) && (s.mode == GESTURE_MODE_DRAW))
    {
        int32_t dx = cx - s.last_x;
        int32_t dy = cy - s.last_y;

        s.move_px += step_px(dx, dy);

        if ((step_px(dx, dy) >= GESTURE_MOVE_MIN_PX))
        {
            push_event(GESTURE_EVT_MOVE, cx, cy, 0);
            s.last_x = cx;
            s.last_y = cy;
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
