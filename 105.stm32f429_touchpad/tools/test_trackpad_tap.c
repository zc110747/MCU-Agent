/**
 ******************************************************************************
 * @file    test_trackpad_tap.c
 * @brief   Host-side test for tap-to-click in app/trackpad.c.
 *
 *  The rule under test: a contact that STARTS inside the pad, is single-finger
 *  the whole time, lasts <= 260 ms and travels < 14 px produces exactly one
 *  left-button click.  Everything else must produce none - a drag, a two-finger
 *  contact, a long press, or a contact that began on a glass key.
 *
 *  app/trackpad.c is compiled for the host with tools/hoststub/ standing in for
 *  FreeRTOS and with usb_mouse_*() replaced by counters, so this runs without a
 *  board and without the USB stack.
 *
 *  Build/run through test_logic.bat.
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "trackpad.h"
#include "usb_mouse.h"

/* ---- Stubs --------------------------------------------------------------- */
TickType_t host_tick = 0;

static int s_crit_depth = 0;      /* must be 0 at the end of every case */
static int s_clicks     = 0;      /* left-button clicks requested        */
static int s_moves      = 0;      /* HID move calls                      */

void host_enter_critical(void) { s_crit_depth++; }
void host_exit_critical(void)  { s_crit_depth--; }
TickType_t xTaskGetTickCount(void) { return host_tick; }

void usb_mouse_move(int16_t dx, int16_t dy)
{
    (void)dx; (void)dy;
}

void usb_mouse_move_ex(int16_t dx, int16_t dy, uint32_t dt_ms)
{
    (void)dx; (void)dy; (void)dt_ms;
    s_moves++;
}

void usb_mouse_set_button(uint8_t mask, int pressed)
{
    (void)mask; (void)pressed;
}

void usb_mouse_click(uint8_t mask)
{
    if ((mask & USB_MOUSE_BTN_LEFT) != 0U)
    {
        s_clicks++;
    }
}

void usb_mouse_release_all(void) { }
uint8_t usb_mouse_buttons(void) { return 0U; }
int usb_mouse_mounted(void) { return 1; }

void usb_mouse_get_status(usb_mouse_status_t *out)
{
    if (out != NULL)
    {
        memset(out, 0, sizeof(*out));
    }
}

void usb_mouse_task(void *arg) { (void)arg; }
int  usb_mouse_init(void) { return 0; }

/* ---- Harness ------------------------------------------------------------- */
#define PAD_W 456
#define PAD_H 380

static int s_pass = 0;
static int s_fail = 0;

static void begin(const char *name)
{
    printf("[CASE] %s\n", name);

    trackpad_clear_area();
    trackpad_set_area(0, 0, PAD_W, PAD_H);
    host_tick = 1000U;
    s_clicks  = 0;
    s_moves   = 0;
}

static void end(int expect_clicks, int expect_moves_nonzero)
{
    int ok = 1;

    if (s_clicks != expect_clicks)
    {
        printf("       clicks: expected %d, got %d\n", expect_clicks, s_clicks);
        ok = 0;
    }
    if ((expect_moves_nonzero != 0) && (s_moves == 0))
    {
        printf("       expected the pointer to move, it did not\n");
        ok = 0;
    }
    if (s_crit_depth != 0)
    {
        printf("       critical section left unbalanced (%d)\n", s_crit_depth);
        ok = 0;
    }

    if (ok) { s_pass++; printf("       ok\n"); }
    else    { s_fail++; printf("       FAILED\n"); }
}

static void feed1(uint16_t x, uint16_t y, uint8_t id)
{
    trackpad_point_t p;

    p.x  = x;
    p.y  = y;
    p.id = id;
    trackpad_feed(&p, 1U);
}

static void feed2(uint16_t x0, uint16_t y0, uint8_t id0,
                  uint16_t x1, uint16_t y1, uint8_t id1)
{
    trackpad_point_t p[2];

    p[0].x = x0; p[0].y = y0; p[0].id = id0;
    p[1].x = x1; p[1].y = y1; p[1].id = id1;
    trackpad_feed(p, 2U);
}

static void release(void)
{
    trackpad_feed(NULL, 0U);
}

/* ---- Cases --------------------------------------------------------------- */
static void case_plain_tap(void)
{
    begin("single tap inside the pad clicks once");

    feed1(100, 100, 0U);
    host_tick += 80U;
    feed1(102, 101, 0U);
    host_tick += 40U;
    release();

    end(1, 1);
}

static void case_drag_is_not_a_click(void)
{
    begin("single-finger drag does not click");

    feed1(100, 100, 0U);
    host_tick += 30U;
    feed1(115, 100, 0U);
    host_tick += 30U;
    feed1(130, 100, 0U);
    host_tick += 30U;
    release();

    end(0, 1);
}

static void case_long_press_is_not_a_click(void)
{
    begin("long press does not click");

    feed1(100, 100, 0U);
    host_tick += 400U;
    release();

    end(0, 0);
}

static void case_two_fingers_is_not_a_click(void)
{
    begin("two-finger contact does not click");

    feed1(100, 100, 0U);
    host_tick += 20U;
    feed2(100, 100, 0U, 200, 200, 1U);
    host_tick += 20U;
    feed2(100, 100, 0U, 200, 200, 1U);
    host_tick += 20U;
    release();

    end(0, 0);
}

static void case_tap_on_a_key_does_not_click(void)
{
    begin("contact starting outside the pad does not click");

    feed1(PAD_W + 10, PAD_H + 10, 0U);
    host_tick += 40U;
    release();

    end(0, 0);
}

static void case_jitter_is_not_a_click(void)
{
    begin("a wiggle that returns to its start is a drag");

    feed1(100, 100, 0U);
    host_tick += 30U;
    feed1(120, 100, 0U);       /* +20 */
    host_tick += 30U;
    feed1(100, 100, 0U);       /* +20 -> 40 px travelled */
    host_tick += 30U;
    release();

    end(0, 1);
}

static void case_tap_after_second_finger_leaves(void)
{
    begin("two fingers, then the second lifts early: no click");

    feed1(100, 100, 0U);
    host_tick += 20U;
    feed2(100, 100, 0U, 200, 200, 1U);
    host_tick += 20U;
    feed1(100, 100, 0U);        /* back to one finger */
    host_tick += 20U;
    release();

    end(0, 0);
}

int main(void)
{
    printf("---- trackpad tap-to-click ----\n");

    case_plain_tap();
    case_drag_is_not_a_click();
    case_long_press_is_not_a_click();
    case_two_fingers_is_not_a_click();
    case_tap_on_a_key_does_not_click();
    case_jitter_is_not_a_click();
    case_tap_after_second_finger_leaves();

    printf("------------------------------\n");
    printf("PASS %d  FAIL %d  -> %s\n", s_pass, s_fail,
           (s_fail == 0) ? "OK" : "FAILED");

    return (s_fail == 0) ? 0 : 1;
}
