/**
 ******************************************************************************
 * @file    trackpad.h
 * @brief   Relative pointer engine: touch samples -> USB HID deltas.
 *
 *  Why this is not done in the UI task
 *  ----------------------------------
 *  The mouse delta used to be computed in app_ui_poll(), i.e. once per LVGL
 *  pump iteration.  Measured on this board that loop runs at ~84 Hz and the
 *  period is not constant (a full-screen flush blocks it for tens of
 *  milliseconds), so the pointer lagged the finger by 12..40 ms and jittered.
 *
 *  The engine therefore lives in its own module and is driven straight from
 *  touch_task, which wakes on the T_PEN interrupt and samples the controller
 *  every TOUCH_POLL_MS.  Nothing between the sample and usb_mouse_move() can
 *  block, so the effective latency is one sample period plus one USB frame.
 *
 *  The UI keeps only the cosmetics: this module publishes the latest contact
 *  and a ring of trail points, which the UI drains whenever it gets a turn.
 ******************************************************************************
 */
#ifndef __TRACKPAD_H__
#define __TRACKPAD_H__

#include <stdint.h>

/* Maximum trail points handed to the UI in one poll.  The producer runs at the
 * touch sample rate (~250 Hz), the consumer at the LVGL rate (~80 Hz), so a
 * handful per pass is the normal case; the ring itself holds 64. */
#define TRACKPAD_TRAIL_MAX      24

/* Contacts per sample the engine accepts (matches the touch controller). */
#define TRACKPAD_MAX_POINTS     5U

typedef struct
{
    uint16_t x;     /* canvas X, already mapped by bsp_touch_scan */
    uint16_t y;
    uint8_t  id;    /* controller track id */
} trackpad_point_t;

typedef struct
{
    uint8_t  active;                    /* finger currently tracked        */
    uint8_t  started;                   /* a new contact began (1 poll)    */
    uint8_t  ended;                     /* the contact was released        */
    int32_t  x;                         /* latest position, canvas px      */
    int32_t  y;
    uint8_t  n;                         /* trail points in tx[] / ty[]     */
    int16_t  tx[TRACKPAD_TRAIL_MAX];
    int16_t  ty[TRACKPAD_TRAIL_MAX];

    /* Wheel strip (see trackpad_set_wheel_area).  Kept apart from the pointer
     * fields so the UI can draw a knob without having to know whether the
     * pointer is frozen. */
    uint8_t  wheel_active;              /* finger is dragging the strip     */
    int32_t  wheel_x;                   /* latest strip position, canvas px */
    int32_t  wheel_y;
    uint32_t wheel_notches;             /* total notches sent since boot    */
} trackpad_visual_t;

typedef struct
{
    uint32_t feeds;     /* samples handed over by touch_task            */
    uint32_t moves;     /* samples that produced a non-zero HID delta   */
    uint32_t clicks;    /* contacts that began inside the pad           */
    uint32_t dropped;   /* trail points lost because the ring was full  */
    uint32_t multi;     /* samples with 2+ contacts (pointer frozen)    */
    uint32_t reanchor;  /* primary finger changed / lifted, no delta    */
    uint32_t outside;   /* contacts whose START was in neither area     */
    uint32_t taps;      /* taps that produced a left-button click       */
    uint32_t wheel;     /* contacts that began inside the wheel strip   */
    uint32_t wheel_notches; /* notches handed to usb_mouse_wheel()      */
} trackpad_stats_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Declare the touch surface in canvas coordinates.
 *         Until this is called (or after trackpad_clear_area()) every sample
 *         is ignored, which is what the splash screen wants.
 */
void trackpad_set_area(int32_t x, int32_t y, int32_t w, int32_t h);

/**
 * @brief  Declare the wheel strip (a second, independent touch region).
 *
 *  A contact belongs to whichever region it STARTED in - never both:
 *    - started inside the pad  -> pointer motion (X and Y)
 *    - started inside the strip -> wheel notches from the Y delta only; the
 *      X delta is ignored entirely and the pointer is not touched
 *    - started elsewhere        -> dropped (glass key, header, ...)
 *
 *  The strip is deliberately Y-only: a horizontal component would either be
 *  discarded noise or, worse, be mistaken for a second axis the host does not
 *  expect from a wheel byte.
 */
void trackpad_set_wheel_area(int32_t x, int32_t y, int32_t w, int32_t h);

void trackpad_clear_area(void);

/**
 * @brief  Feed one sample.  MUST be called from a single task (touch_task);
 *         the only work it does is arithmetic plus a short critical section
 *         around the trail ring, so it never blocks.
 *
 *  Multi-touch policy (this is what keeps the pointer usable):
 *    - n == 1: the pointer follows that finger, by track id.  A different id
 *      taking over is a re-anchor, not a movement: the last position is
 *      silently replaced instead of sending a huge HID delta.
 *    - n >= 2: two-finger gesture territory.  The pointer is FROZEN - it does
 *      not move and no trail is drawn - otherwise a scroll would drag the
 *      cursor across the desktop.
 *    - n == 0: release, as before.  A contact that began inside the pad, was
 *      single-finger the whole time, lasted <= 260 ms and travelled < 14 px is
 *      a TAP: it emits one left-button click (tap-to-click).
 *    - the wheel strip (trackpad_set_wheel_area) is a separate region: only
 *      contacts that started there produce notches, and those never move the
 *      pointer nor click.  Two fingers on the strip freeze it like the pad.
 *
 * @param  pts  contacts of this sample, already mapped onto the canvas
 * @param  n    number of valid entries (0 = every finger is up)
 */
void trackpad_feed(const trackpad_point_t *pts, uint8_t n);

/**
 * @brief  Drain the visual state for the UI task.  @p out is fully written.
 */
void trackpad_poll(trackpad_visual_t *out);

void trackpad_get_stats(trackpad_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* __TRACKPAD_H__ */
