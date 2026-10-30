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

static volatile int32_t s_area_x = 0;
static volatile int32_t s_area_y = 0;
static volatile int32_t s_area_w = 0;
static volatile int32_t s_area_h = 0;
static volatile uint8_t s_area_valid = 0U;

/* Contact state (producer only). */
static uint8_t s_active = 0U;
static int32_t s_last_x = 0;
static int32_t s_last_y = 0;
static TickType_t s_last_tick = 0;
static uint8_t s_started = 0U;
static uint8_t s_ended   = 0U;

/* Latest position for the on-glass dot (producer writes, UI reads). */
static volatile int32_t s_dot_x = 0;
static volatile int32_t s_dot_y = 0;

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

void trackpad_clear_area(void)
{
    s_area_valid = 0U;
    s_active     = 0U;
    s_started    = 0U;
    s_ended      = 0U;

    taskENTER_CRITICAL();
    s_ring_tail = s_ring_head;
    taskEXIT_CRITICAL();
}

void trackpad_feed(uint16_t x, uint16_t y, uint8_t pressed)
{
    int32_t lx;
    int32_t ly;
    int32_t dx;
    int32_t dy;

    if (s_area_valid == 0U)
    {
        return;
    }

    s_feeds++;

    lx = (int32_t)x - s_area_x;
    ly = (int32_t)y - s_area_y;

    /* ---- Release -------------------------------------------------------- */
    if (pressed == 0U)
    {
        if (s_active != 0U)
        {
            s_active = 0U;
            s_ended  = 1U;
        }
        return;
    }

    /* ---- Already tracking ----------------------------------------------- *
     * Once a contact began inside the pad it keeps moving the pointer even if
     * the finger wanders over a button or past the pad border: stopping at the
     * edge is what makes a trackpad feel "sticky".  A contact that *starts* on
     * a button never enters this branch, so tapping a glass key cannot drag
     * the cursor. */
    if (s_active != 0U)
    {
        TickType_t now = xTaskGetTickCount();
        uint32_t   dt  = (uint32_t)(now - s_last_tick);

        dx = lx - s_last_x;
        dy = ly - s_last_y;

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

    /* ---- New contact: only starts when it lands inside the pad ---------- */
    if ((lx < 0) || (ly < 0) || (lx >= s_area_w) || (ly >= s_area_h))
    {
        return;
    }

    s_active  = 1U;
    s_started = 1U;
    s_last_x  = lx;
    s_last_y  = ly;
    s_last_tick = xTaskGetTickCount();
    s_dot_x   = lx;
    s_dot_y   = ly;
    s_clicks++;

    taskENTER_CRITICAL();
    ring_push(lx, ly);
    taskEXIT_CRITICAL();
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
}
