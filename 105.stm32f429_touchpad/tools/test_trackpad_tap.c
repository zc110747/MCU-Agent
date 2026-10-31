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
static int s_notch_sum  = 0;      /* signed wheel notches requested      */
static int s_notch_calls = 0;     /* usb_mouse_wheel() invocations       */

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

void usb_mouse_wheel(int8_t notches)
{
    s_notch_sum += (int)notches;
    s_notch_calls++;
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

/* Wheel strip, placed where the UI puts it relative to the pad: to the right
 * of the status card, well clear of the pad itself. */
#define WHL_X 520
#define WHL_Y 500
#define WHL_W 96
#define WHL_H 196

/* Notches per 18 px of travel is a property of app/trackpad.c; the tests below
 * spell the distances out so a change there shows up as a failure here. */
#define NOTCH_PX 18

static int s_pass = 0;
static int s_fail = 0;

static void begin(const char *name)
{
    printf("[CASE] %s\n", name);

    trackpad_clear_area();
    trackpad_set_area(0, 0, PAD_W, PAD_H);
    trackpad_set_wheel_area(WHL_X, WHL_Y, WHL_W, WHL_H);
    host_tick = 1000U;
    s_clicks  = 0;
    s_moves   = 0;
    s_notch_sum = 0;
    s_notch_calls = 0;
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
    if (s_notch_sum != 0)
    {
        printf("       expected no wheel notches, got %d\n", s_notch_sum);
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

/* Wheel cases: a strip contact must produce notches and nothing else. */
static void end_wheel(int expect_notches)
{
    int ok = 1;

    if (s_notch_sum != expect_notches)
    {
        printf("       notches: expected %d, got %d\n", expect_notches, s_notch_sum);
        ok = 0;
    }
    if (s_clicks != 0)
    {
        printf("       the strip must not click, got %d\n", s_clicks);
        ok = 0;
    }
    if (s_moves != 0)
    {
        printf("       the strip must not move the pointer, got %d\n", s_moves);
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

/* ---- Wheel strip cases --------------------------------------------------- */
static void case_strip_drag_up_scrolls_up(void)
{
    begin("2 notches of upward travel -> +2 wheel");

    feed1(WHL_X + 48, WHL_Y + 150, 0U);
    host_tick += 10U;
    feed1(WHL_X + 48, WHL_Y + 150 - NOTCH_PX, 0U);
    host_tick += 10U;
    feed1(WHL_X + 48, WHL_Y + 150 - 2 * NOTCH_PX, 0U);
    host_tick += 10U;
    release();

    end_wheel(2);
}

static void case_strip_drag_down_scrolls_down(void)
{
    begin("1 notch of downward travel -> -1 wheel");

    feed1(WHL_X + 48, WHL_Y + 40, 0U);
    host_tick += 10U;
    feed1(WHL_X + 48, WHL_Y + 40 + NOTCH_PX, 0U);
    host_tick += 10U;
    release();

    end_wheel(-1);
}

static void case_strip_remainder_is_carried(void)
{
    begin("10 px + 8 px is still one notch (remainder carried)");

    feed1(WHL_X + 48, WHL_Y + 120, 0U);
    host_tick += 10U;
    feed1(WHL_X + 48, WHL_Y + 110, 0U);     /* 10 px, no notch yet */
    host_tick += 10U;
    feed1(WHL_X + 48, WHL_Y + 102, 0U);     /* 18 px total        */
    host_tick += 10U;
    release();

    end_wheel(1);
}

static void case_strip_ignores_x(void)
{
    begin("horizontal motion in the strip does nothing");

    feed1(WHL_X + 10, WHL_Y + 100, 0U);
    host_tick += 10U;
    feed1(WHL_X + 80, WHL_Y + 100, 0U);
    host_tick += 10U;
    feed1(WHL_X + 10, WHL_Y + 100, 0U);
    host_tick += 10U;
    release();

    end_wheel(0);
}

static void case_strip_two_fingers_do_not_scroll(void)
{
    begin("a second finger on the strip freezes it");

    feed1(WHL_X + 48, WHL_Y + 150, 0U);
    host_tick += 10U;
    feed2(WHL_X + 48, WHL_Y + 150, 0U, WHL_X + 20, WHL_Y + 60, 1U);
    host_tick += 10U;
    feed2(WHL_X + 48, WHL_Y + 100, 0U, WHL_X + 20, WHL_Y + 20, 1U);
    host_tick += 10U;
    release();

    end_wheel(0);
}

static void case_pad_contact_never_scrolls(void)
{
    begin("a pad drag produces motion, no notches");

    feed1(100, 100, 0U);
    host_tick += 10U;
    feed1(100, 60, 0U);
    host_tick += 10U;
    feed1(100, 20, 0U);
    host_tick += 10U;
    release();

    end(0, 1);
}

static void case_strip_tap_does_not_click(void)
{
    begin("a quick tap on the strip is not a left click");

    feed1(WHL_X + 48, WHL_Y + 100, 0U);
    host_tick += 40U;
    release();

    end_wheel(0);
}

static void case_strip_start_needs_the_strip(void)
{
    begin("a contact between pad and strip does nothing");

    /* x is right of the pad (456) but left of the strip (520). */
    feed1(480, 300, 0U);
    host_tick += 10U;
    feed1(480, 260, 0U);
    host_tick += 10U;
    release();

    end_wheel(0);
}

int main(void)
{
    printf("---- trackpad tap-to-click + wheel strip ----\n");

    case_plain_tap();
    case_drag_is_not_a_click();
    case_long_press_is_not_a_click();
    case_two_fingers_is_not_a_click();
    case_tap_on_a_key_does_not_click();
    case_jitter_is_not_a_click();
    case_tap_after_second_finger_leaves();

    case_strip_drag_up_scrolls_up();
    case_strip_drag_down_scrolls_down();
    case_strip_remainder_is_carried();
    case_strip_ignores_x();
    case_strip_two_fingers_do_not_scroll();
    case_pad_contact_never_scrolls();
    case_strip_tap_does_not_click();
    case_strip_start_needs_the_strip();

    printf("------------------------------\n");
    printf("PASS %d  FAIL %d  -> %s\n", s_pass, s_fail,
           (s_fail == 0) ? "OK" : "FAILED");

    return (s_fail == 0) ? 0 : 1;
}
