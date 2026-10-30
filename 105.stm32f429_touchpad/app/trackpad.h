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
} trackpad_visual_t;

typedef struct
{
    uint32_t feeds;     /* samples handed over by touch_task            */
    uint32_t moves;     /* samples that produced a non-zero HID delta   */
    uint32_t clicks;    /* contacts that began inside the pad           */
    uint32_t dropped;   /* trail points lost because the ring was full  */
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
void trackpad_clear_area(void);

/**
 * @brief  Feed one sample.  MUST be called from a single task (touch_task);
 *         the only work it does is arithmetic plus a short critical section
 *         around the trail ring, so it never blocks.
 * @param  x,y      canvas coordinates (already mapped by bsp_touch_scan)
 * @param  pressed  non-zero while at least one contact is down
 */
void trackpad_feed(uint16_t x, uint16_t y, uint8_t pressed);

/**
 * @brief  Drain the visual state for the UI task.  @p out is fully written.
 */
void trackpad_poll(trackpad_visual_t *out);

void trackpad_get_stats(trackpad_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* __TRACKPAD_H__ */
