/**
 ******************************************************************************
 * @file    gesture.h
 * @brief   Touch-panel gesture engine: contact samples in, UI events out.
 *
 *  Supported gestures (tablet / trackpad conventions):
 *
 *    one finger drag       -> PRESS / MOVE / RELEASE   (the UI draws a stroke)
 *    one finger tap        -> TAP                      (stamp a dot)
 *    two fingers vertical  -> SCROLL notch             (dy, +/- notches)
 *    two fingers horizontal-> PAN notch                (dx, +/- notches)
 *    two finger tap        -> TAP2
 *    three finger tap      -> TAP3
 *
 *  Multi-touch model
 *  -----------------
 *  Every sample carries the controller's track id per contact.  The engine
 *  keeps a small table of live ids and uses it for two things:
 *
 *    - the PRIMARY finger (the one that has been down the longest) drives the
 *      stroke, so adding a second finger never makes the ink jump to it;
 *    - scroll / pan use the per-id delta of the fingers present in BOTH frames,
 *      not the centroid of the frame.  A third finger landing or the two
 *      fingers drifting apart therefore no longer produces a phantom notch.
 *
 *  The engine is a pure state machine: it never touches hardware, never blocks
 *  and owns no task.  The touch task feeds it one sample per poll; the UI task
 *  drains the event queue while pumping LVGL, so every event is consumed in the
 *  LVGL thread and no widget API is ever called from the input side.
 ******************************************************************************
 */
#ifndef __GESTURE_H__
#define __GESTURE_H__

#include <stdint.h>

#define GESTURE_MAX_POINTS   5U

typedef struct
{
    uint16_t x;     /* canvas X */
    uint16_t y;     /* canvas Y */
    uint8_t  id;    /* controller track id, stable while the finger stays down */
    uint8_t  size;  /* contact area, 0..255                                    */
} gesture_point_t;

/* ---- Events -------------------------------------------------------------- */
typedef enum
{
    GESTURE_EVT_PRESS = 0,  /* first single-finger contact, (x,y)            */
    GESTURE_EVT_MOVE,       /* single-finger move, (x,y)                     */
    GESTURE_EVT_RELEASE,    /* stroke finished, (x,y)                        */
    GESTURE_EVT_TAP,        /* one-finger tap, (x,y)                         */
    GESTURE_EVT_TAP2,       /* two-finger tap, (x,y)                         */
    GESTURE_EVT_TAP3,       /* three-finger tap, (x,y)                       */
    GESTURE_EVT_SCROLL,     /* two-finger vertical, d = notches (+down)      */
    GESTURE_EVT_PAN,        /* two-finger horizontal, d = notches (+right)   */
    GESTURE_EVT_NONE
} gesture_evt_type_t;

typedef struct
{
    uint8_t  type;      /* gesture_evt_type_t                            */
    int16_t  x;         /* canvas position of the gesture centroid       */
    int16_t  y;
    int16_t  d;         /* notches for SCROLL / PAN, 0 otherwise         */
} gesture_evt_t;

typedef struct
{
    uint8_t  fingers;       /* contacts in the newest sample            */
    uint32_t presses;       /* strokes started                          */
    uint32_t taps;          /* 1-finger taps                            */
    uint32_t multi_taps;    /* 2/3-finger taps                          */
    uint32_t scrolls;       /* wheel notches                            */
    uint32_t pans;          /* horizontal notches                       */
    uint32_t dropped;       /* events lost because the queue was full   */
    uint8_t  active;        /* 1 while at least one contact is reported */
} gesture_status_t;

#ifdef __cplusplus
extern "C" {
#endif

/** Reset the state machine and the counters. */
void gesture_init(void);

/**
  * @brief  Feed one sample of contacts, already mapped onto the LCD canvas.
  * @param  now_ms  monotonic milliseconds (HAL_GetTick())
  * @param  pts     contact coordinates
  * @param  n       number of valid entries in @p pts (0 = no contact)
  */
void gesture_feed(uint32_t now_ms, const gesture_point_t *pts, uint8_t n);

/**
  * @brief  Pop one pending event, oldest first.
  * @retval 1 when @p out was filled, 0 when the queue is empty.
  */
int gesture_poll(gesture_evt_t *out);

/** Copy the status snapshot. */
void gesture_get_status(gesture_status_t *out);

#ifdef __cplusplus
}
#endif

#endif /* __GESTURE_H__ */
