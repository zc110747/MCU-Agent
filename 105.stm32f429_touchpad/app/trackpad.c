/**
 ******************************************************************************
 * @file    trackpad.c
 * @brief   Relative pointer engine (see trackpad.h for the rationale).
 *
 *  Data flow
 *  ---------
 *      touch_task              trackpad            usb_mouse_task
 *      ----------              --------            --------------
 *      bsp_touch_scan()  --->  feed():             mouse_send()
 *                              delta = x - last          ^
 *                              usb_mouse_move()  --------+
 *                              push trail point
 *
 *      app_ui (LVGL)     <---  poll(): latest dot + new trail points
 *
 *  One producer (touch task) and one consumer (UI task).  The trail ring is
 *  guarded by taskENTER_CRITICAL() because the UI may preempt in the middle of
 *  a push; everything else is a single word and needs no lock.
 ******************************************************************************
 */
#include <stddef.h>

#include "FreeRTOS.h"
#include "task.h"

#include "trackpad.h"
#include "usb_mouse.h"

#define TP_RING_SIZE    64

/* Tap-to-click.  A contact counts as a tap when it is short and stayed close to
 * where it landed; the thresholds are the same ones the gesture engine uses so
 * the ink dot and the mouse click agree about what "a tap" is.  It only ever
 * fires for contacts that STARTED inside the pad, so tapping a glass key never
 * produces a mouse click as well. */
#define TP_TAP_MS        260U
#define TP_TAP_MOVE_PX   14

/* Wheel strip: pixels of finger travel per notch.  A wheel notch is one text
 * line (~40 px on screen), which would feel far too coarse if it were 1:1, and
 * 18 px keeps a slow drag from emitting a notch per sample.  Because the
 * remainder is carried (not reset per sample), the notch rate is exactly
 * travel/18 regardless of how finely the motion was sampled. */
#define TP_WHEEL_NOTCH_PX 18

/* +1 because a finger moving up must scroll up (towards the start of the
 * document), which is the same direction as pushing a scrollbar thumb up. */
#define TP_WHEEL_INVERT   0

typedef enum
{
    TP_MODE_NONE = 0,   /* no contact, or the contact started nowhere useful  */
    TP_MODE_POINTER,    /* contact that began inside the touch surface        */
    TP_MODE_WHEEL       /* contact that began inside the wheel strip          */
} tp_mode_t;

static volatile int32_t s_area_x = 0;
static volatile int32_t s_area_y = 0;
static volatile int32_t s_area_w = 0;
static volatile int32_t s_area_h = 0;
static volatile uint8_t s_area_valid = 0U;

/* Wheel strip geometry (canvas coordinates). */
static volatile int32_t s_whl_x = 0;
static volatile int32_t s_whl_y = 0;
static volatile int32_t s_whl_w = 0;
static volatile int32_t s_whl_h = 0;
static volatile uint8_t s_whl_valid = 0U;

/* Contact state (producer only). */
static tp_mode_t s_mode = TP_MODE_NONE;
static uint8_t  s_active = 0U;
static int32_t s_last_x = 0;
static int32_t s_last_y = 0;
static uint8_t  s_primary_id = 0U;
static uint8_t  s_have_primary = 0U;
static TickType_t s_last_tick = 0;
static uint8_t s_started = 0U;
static uint8_t s_ended   = 0U;

/* Contact bookkeeping for tap-to-click (producer only). */
static TickType_t s_contact_tick = 0;
static int32_t    s_travel_px    = 0;
static uint8_t    s_peak_n       = 1U;

/* Wheel strip contact state (producer only).  The notch remainder survives
 * between samples but not between contacts: a new finger must not inherit half
 * a notch from the previous one. */
static int32_t  s_whl_last_y    = 0;
static int32_t  s_whl_acc       = 0;
static uint8_t  s_whl_primary_id = 0U;
static uint8_t  s_whl_have_id    = 0U;

/* Latest position for the on-glass dot (producer writes, UI reads). */
static volatile int32_t s_dot_x = 0;
static volatile int32_t s_dot_y = 0;

/* Latest wheel-strip position + notch total (producer writes, UI reads). */
static volatile int32_t  s_whl_dot_x = 0;
static volatile int32_t  s_whl_dot_y = 0;
static volatile uint8_t  s_whl_active = 0U;
static volatile uint32_t s_whl_notches = 0U;

/* Trail ring. */
static int16_t s_ring_x[TP_RING_SIZE];
static int16_t s_ring_y[TP_RING_SIZE];
static volatile uint8_t s_ring_head = 0U;   /* producer */
static volatile uint8_t s_ring_tail = 0U;   /* consumer */

/* Statistics. */
static volatile uint32_t s_feeds   = 0U;
static volatile uint32_t s_moves   = 0U;
static volatile uint32_t s_clicks  = 0U;
static volatile uint32_t s_dropped = 0U;
static volatile uint32_t s_multi   = 0U;
static volatile uint32_t s_reanchor = 0U;
static volatile uint32_t s_outside = 0U;
static volatile uint32_t s_taps    = 0U;
static volatile uint32_t s_wheel_contacts = 0U;
static volatile uint32_t s_wheel_notches   = 0U;

/* -------------------------------------------------------------------------- */
/* Trail ring                                                                 */
/* -------------------------------------------------------------------------- */
/* Caller must hold a critical section. */
static void ring_push(int32_t x, int32_t y)
{
    uint8_t next = (uint8_t)((s_ring_head + 1U) % TP_RING_SIZE);

    if (next == s_ring_tail)
    {
        /* Full: the UI is behind.  Dropping the newest point keeps the line
         * continuous instead of tearing a hole in the middle of it. */
        s_dropped++;
        return;
    }

    s_ring_x[s_ring_head] = (int16_t)x;
    s_ring_y[s_ring_head] = (int16_t)y;
    s_ring_head = next;
}

/* -------------------------------------------------------------------------- */
/* Producer                                                                   */
/* -------------------------------------------------------------------------- */
void trackpad_set_area(int32_t x, int32_t y, int32_t w, int32_t h)
{
    if ((w <= 0) || (h <= 0))
    {
        trackpad_clear_area();
        return;
    }

    s_area_x = x;
    s_area_y = y;
    s_area_w = w;
    s_area_h = h;
    s_area_valid = 1U;
}

void trackpad_set_wheel_area(int32_t x, int32_t y, int32_t w, int32_t h)
{
    if ((w <= 0) || (h <= 0))
    {
        s_whl_valid = 0U;
        return;
    }

    s_whl_x = x;
    s_whl_y = y;
    s_whl_w = w;
    s_whl_h = h;
    s_whl_valid = 1U;

    /* Park the knob in the middle until the first drag. */
    s_whl_dot_x = x + (w / 2);
    s_whl_dot_y = y + (h / 2);
}

void trackpad_clear_area(void)
{
    s_area_valid = 0U;
    s_whl_valid  = 0U;
    s_mode       = TP_MODE_NONE;
    s_active     = 0U;
    s_started    = 0U;
    s_ended      = 0U;
    s_have_primary = 0U;
    s_travel_px    = 0;
    s_peak_n       = 1U;
    s_whl_acc      = 0;
    s_whl_have_id  = 0U;
    s_whl_active   = 0U;

    taskENTER_CRITICAL();
    s_ring_tail = s_ring_head;
    taskEXIT_CRITICAL();
}

/**
 * @brief  Turn finger travel inside the wheel strip into wheel notches.
 *
 *  Only the Y delta is used - that is the whole point of a dedicated strip:
 *  horizontal wobble during a vertical drag must not turn into anything, and
 *  there is exactly one axis a 1-byte wheel can carry.
 */
static void wheel_track(const trackpad_point_t *p)
{
    int32_t  ly = (int32_t)p->y;
    int32_t  dy;
    int32_t  notch;

    /* A different track id means the finger that owned the strip is gone and
     * this is the survivor of a multi-finger contact: adopt its position
     * silently instead of reporting the distance between two fingers. */
    if ((s_whl_have_id == 0U) || (p->id != s_whl_primary_id))
    {
        s_whl_primary_id = p->id;
        s_whl_have_id    = 1U;
        s_whl_last_y     = ly;
        s_whl_dot_x      = (int32_t)p->x;
        s_whl_dot_y      = ly;
        return;
    }

    dy = ly - s_whl_last_y;
    s_whl_last_y = ly;
    s_whl_dot_x  = (int32_t)p->x;
    s_whl_dot_y  = ly;

    if (dy == 0)
    {
        return;
    }

    s_whl_acc += dy;

    /* Emit every whole notch that has accumulated.  A finger moving up the
     * glass (dy < 0) scrolls up, i.e. a positive wheel value. */
    while ((s_whl_acc <= -TP_WHEEL_NOTCH_PX) || (s_whl_acc >= TP_WHEEL_NOTCH_PX))
    {
        if (s_whl_acc <= -TP_WHEEL_NOTCH_PX)
        {
            notch = 1;
            s_whl_acc += TP_WHEEL_NOTCH_PX;
        }
        else
        {
            notch = -1;
            s_whl_acc -= TP_WHEEL_NOTCH_PX;
        }

#if TP_WHEEL_INVERT
        notch = -notch;
#endif

        usb_mouse_wheel((int8_t)notch);
        s_whl_notches++;
        s_wheel_notches++;
    }
}

void trackpad_feed(const trackpad_point_t *pts, uint8_t n)
{
    int32_t lx;
    int32_t ly;
    int32_t dx;
    int32_t dy;

    if (s_area_valid == 0U)
    {
        return;
    }

    if ((pts == NULL) || (n == 0U))
    {
        n = 0U;
    }
    if (n > TRACKPAD_MAX_POINTS)
    {
        n = TRACKPAD_MAX_POINTS;
    }

    s_feeds++;

    /* ---- Release -------------------------------------------------------- */
    if (n == 0U)
    {
        s_have_primary = 0U;
        s_whl_have_id  = 0U;

        if (s_mode == TP_MODE_WHEEL)
        {
            /* End of a scroll drag.  The knob stays where it was released and
             * the notch remainder is dropped, so the next contact starts from
             * a clean slate instead of inheriting half a notch. */
            s_whl_active = 0U;
            s_whl_acc    = 0;
            s_mode       = TP_MODE_NONE;
            return;
        }

        if (s_active != 0U)
        {
            uint32_t held = (uint32_t)(xTaskGetTickCount() - s_contact_tick);

            s_active = 0U;
            s_ended  = 1U;

            /* Tap-to-click.  "Short and it stayed put" is the whole rule, and
             * it must have been a single finger the entire time: a two-finger
             * scroll that happens to end quickly is not a click.  The contact
             * had to begin inside the pad to get here at all, which is what
             * keeps the glass buttons from double-firing. */
            if ((held <= TP_TAP_MS) && (s_travel_px < TP_TAP_MOVE_PX) && (s_peak_n == 1U))
            {
                usb_mouse_click(USB_MOUSE_BTN_LEFT);
                s_taps++;
            }
        }

        s_mode = TP_MODE_NONE;
        return;
    }

    /* ---- Two or more fingers: gesture territory, pointer frozen ---------- *
     * Without this the centroid of a two-finger scroll would drag the cursor
     * across the desktop - the classic "trackpad scrolls and clicks things"
     * failure.  The position is re-anchored below, on the frame where a single
     * finger is left again, so the freeze cannot turn into a jump either.
     * The same applies to the wheel strip: a second finger landing while the
     * user scrolls must not add half a notch. */
    if (n >= 2U)
    {
        s_multi++;
        s_peak_n       = 2U;    /* this contact is no longer a tap candidate */
        s_have_primary = 0U;    /* the remaining finger must not inherit this */
        s_whl_have_id  = 0U;
        return;
    }

    /* ---- Wheel strip owns contacts that started in it -------------------- */
    if (s_mode == TP_MODE_WHEEL)
    {
        wheel_track(&pts[0]);
        return;
    }

    lx = (int32_t)pts[0].x - s_area_x;
    ly = (int32_t)pts[0].y - s_area_y;

    /* ---- Already tracking ----------------------------------------------- *
     * Once a contact began inside the pad it keeps moving the pointer even if
     * the finger wanders over a button or past the pad border: stopping at the
     * edge is what makes a trackpad feel "sticky".  A contact that *starts* on
     * a button never enters this branch, so tapping a glass key cannot drag
     * the cursor. */
    if (s_mode == TP_MODE_POINTER)
    {
        TickType_t now = xTaskGetTickCount();
        uint32_t   dt  = (uint32_t)(now - s_last_tick);

        if ((s_have_primary == 0U) || (pts[0].id != s_primary_id))
        {
            /* The finger that owned the pointer is gone (or a second finger
             * just lifted and this is the survivor).  Take the new finger over
             * at its current position instead of reporting the distance from
             * the previous owner's position, which would be a visible jump. */
            s_reanchor++;
            s_primary_id   = pts[0].id;
            s_have_primary = 1U;
            s_last_x       = lx;
            s_last_y       = ly;
            s_last_tick    = now;

            if ((lx >= 0) && (ly >= 0) && (lx < s_area_w) && (ly < s_area_h))
            {
                s_dot_x = lx;
                s_dot_y = ly;
            }
            return;
        }

        dx = lx - s_last_x;
        dy = ly - s_last_y;

        /* Absolute travel, for tap detection.  Summing |dx|+|dy| means a slow
         * wiggle that ends where it started still counts as movement, so it
         * becomes a (tiny) drag instead of a click. */
        s_travel_px += ((dx < 0) ? -dx : dx) + ((dy < 0) ? -dy : dy);

        if ((dx == 0) && (dy == 0))
        {
            return;
        }

        /* Speed-normalised: the acceleration curve wants px/s, and the sample
         * period is not constant (the idle wake-up path is much slower than the
         * contact loop), so hand the elapsed time over instead of pretending
         * every sample took the same 5 ms. */
        usb_mouse_move_ex((int16_t)dx, (int16_t)dy, dt);
        s_last_tick = now;
        s_last_x = lx;
        s_last_y = ly;
        s_moves++;

        /* Keep the dot inside the pad; the pointer itself is free to move. */
        if (lx < 0)          { lx = 0; }
        if (ly < 0)          { ly = 0; }
        if (lx >= s_area_w)  { lx = s_area_w - 1; }
        if (ly >= s_area_h)  { ly = s_area_h - 1; }
        s_dot_x = lx;
        s_dot_y = ly;

        taskENTER_CRITICAL();
        ring_push(s_last_x, s_last_y);
        taskEXIT_CRITICAL();
        return;
    }

    /* ---- New contact: the region it LANDS in decides what it does -------- *
     * Pad first, wheel strip second, everything else is dropped.  Checking the
     * pad first is what keeps the strip from stealing a finger that starts
     * next to it, and vice versa. */
    if ((lx >= 0) && (ly >= 0) && (lx < s_area_w) && (ly < s_area_h))
    {
        s_mode    = TP_MODE_POINTER;
        s_active  = 1U;
        s_started = 1U;
        s_primary_id   = pts[0].id;
        s_have_primary = 1U;
        s_last_x  = lx;
        s_last_y  = ly;
        s_last_tick = xTaskGetTickCount();
        s_contact_tick = s_last_tick;
        s_travel_px    = 0;
        s_peak_n       = 1U;
        s_dot_x   = lx;
        s_dot_y   = ly;
        s_clicks++;

        taskENTER_CRITICAL();
        ring_push(lx, ly);
        taskEXIT_CRITICAL();
        return;
    }

    if (s_whl_valid != 0U)
    {
        int32_t wx = (int32_t)pts[0].x - s_whl_x;
        int32_t wy = (int32_t)pts[0].y - s_whl_y;

        if ((wx >= 0) && (wy >= 0) && (wx < s_whl_w) && (wy < s_whl_h))
        {
            s_mode       = TP_MODE_WHEEL;
            s_whl_primary_id = pts[0].id;
            s_whl_have_id    = 1U;
            s_whl_last_y     = (int32_t)pts[0].y;
            s_whl_acc        = 0;
            s_whl_active     = 1U;
            s_whl_dot_x      = (int32_t)pts[0].x;
            s_whl_dot_y      = (int32_t)pts[0].y;
            s_wheel_contacts++;
            return;
        }
    }

    /* Counted so the status dump can tell "the finger never reached the pad"
     * (huge count) from "the pad area is wrong" (0) - both look the same from
     * the outside, namely: the pointer does not move. */
    s_outside++;
}

/* -------------------------------------------------------------------------- */
/* Consumer                                                                   */
/* -------------------------------------------------------------------------- */
void trackpad_poll(trackpad_visual_t *out)
{
    uint8_t head;

    if (out == NULL)
    {
        return;
    }

    out->n = 0U;

    /* Read-and-clear under a critical section: the producer runs at a higher
     * priority, so without it a "started" flag raised between the read and the
     * clear would be lost and the UI would never reset the trail. */
    taskENTER_CRITICAL();
    out->active  = s_active;
    out->started = s_started;
    out->ended   = s_ended;
    out->x       = s_dot_x;
    out->y       = s_dot_y;
    out->wheel_active  = s_whl_active;
    out->wheel_x       = s_whl_dot_x;
    out->wheel_y       = s_whl_dot_y;
    out->wheel_notches = s_whl_notches;
    s_started    = 0U;
    s_ended      = 0U;

    head = s_ring_head;
    while ((s_ring_tail != head) && (out->n < TRACKPAD_TRAIL_MAX))
    {
        out->tx[out->n] = s_ring_x[s_ring_tail];
        out->ty[out->n] = s_ring_y[s_ring_tail];
        out->n++;
        s_ring_tail = (uint8_t)((s_ring_tail + 1U) % TP_RING_SIZE);
    }
    taskEXIT_CRITICAL();
}

void trackpad_get_stats(trackpad_stats_t *out)
{
    if (out == NULL)
    {
        return;
    }

    out->feeds   = s_feeds;
    out->moves   = s_moves;
    out->clicks  = s_clicks;
    out->dropped = s_dropped;
    out->multi   = s_multi;
    out->reanchor = s_reanchor;
    out->outside = s_outside;
    out->taps    = s_taps;
    out->wheel   = s_wheel_contacts;
    out->wheel_notches = s_wheel_notches;
}
