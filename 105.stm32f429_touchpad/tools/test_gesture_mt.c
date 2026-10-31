/**
 * Host-side test for the multi-touch behaviour of app/gesture.c.
 *
 * gesture.c is a pure state machine (no hardware, no RTOS), so it can be
 * compiled and exercised with the host gcc.  That matters here because the
 * interesting multi-touch bugs are logic bugs, not wiring bugs:
 *
 *   - a second finger landing used to change the buffer order and the ink
 *     jumped to the new finger;
 *   - scroll/pan used the frame centroid, so a finger landing or drifting away
 *     produced phantom wheel notches;
 *   - two-finger taps were counted from the wrong contact count.
 *
 * Build + run (see tools/test_gesture_mt.sh):
 *     gcc -std=c99 -Wall -Wextra -I app tools/test_gesture_mt.c app/gesture.c
 */
#include <stdio.h>
#include <string.h>

#include "gesture.h"

static int s_pass;
static int s_fail;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { s_pass++; }                                                \
        else {                                                                 \
            s_fail++;                                                          \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                      \
    } while (0)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        int _a = (int)(a);                                                     \
        int _b = (int)(b);                                                     \
        if (_a == _b) { s_pass++; }                                            \
        else {                                                                 \
            s_fail++;                                                          \
            printf("  FAIL %s:%d  %s=%d != %s=%d\n",                           \
                   __FILE__, __LINE__, #a, _a, #b, _b);                        \
        }                                                                      \
    } while (0)

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */
static int drain(gesture_evt_t *out, int max)
{
    int n = 0;

    while ((n < max) && (gesture_poll(&out[n]) != 0))
    {
        n++;
    }
    return n;
}

static int count_type(const gesture_evt_t *ev, int n, uint8_t type)
{
    int i;
    int c = 0;

    for (i = 0; i < n; i++)
    {
        if (ev[i].type == type)
        {
            c++;
        }
    }
    return c;
}

static void pt(gesture_point_t *p, uint8_t id, uint16_t x, uint16_t y)
{
    p->id   = id;
    p->x    = x;
    p->y    = y;
    p->size = 20U;
}

/* -------------------------------------------------------------------------- */
/* 1. single finger: the classic stroke still works                           */
/* -------------------------------------------------------------------------- */
static void test_single_finger_drag(void)
{
    gesture_evt_t ev[16];
    gesture_point_t p[GESTURE_MAX_POINTS];
    int n;

    printf("[1] single finger drag\n");
    gesture_init();

    pt(&p[0], 0U, 100U, 100U);
    gesture_feed(0U, p, 1U);
    n = drain(ev, 16);
    CHECK_EQ(count_type(ev, n, GESTURE_EVT_PRESS), 1);

    pt(&p[0], 0U, 130U, 100U);
    gesture_feed(5U, p, 1U);
    n = drain(ev, 16);
    CHECK_EQ(count_type(ev, n, GESTURE_EVT_MOVE), 1);
    CHECK_EQ(ev[0].x, 130);

    pt(&p[0], 0U, 160U, 120U);
    gesture_feed(10U, p, 1U);
    (void)drain(ev, 16);

    gesture_feed(15U, NULL, 0U);
    n = drain(ev, 16);
    CHECK_EQ(count_type(ev, n, GESTURE_EVT_RELEASE), 1);
    CHECK_EQ(ev[0].x, 160);
    CHECK_EQ(ev[0].y, 120);
}

/* -------------------------------------------------------------------------- */
/* 2. the second finger must not steal the stroke                             */
/* -------------------------------------------------------------------------- */
static void test_second_finger_no_jump(void)
{
    gesture_evt_t ev[16];
    gesture_point_t p[GESTURE_MAX_POINTS];
    int n;
    int i;
    int moves = 0;

    printf("[2] second finger does not move the ink\n");
    gesture_init();

    pt(&p[0], 0U, 100U, 100U);
    gesture_feed(0U, p, 1U);
    (void)drain(ev, 16);

    pt(&p[0], 0U, 110U, 100U);
    gesture_feed(5U, p, 1U);
    (void)drain(ev, 16);

    /* Second finger lands far away, first finger does not move. */
    pt(&p[0], 0U, 110U, 100U);
    pt(&p[1], 1U, 400U, 700U);
    gesture_feed(10U, p, 2U);
    n = drain(ev, 16);
    CHECK_EQ(count_type(ev, n, GESTURE_EVT_RELEASE), 1);
    CHECK_EQ(ev[0].x, 110);
    CHECK_EQ(ev[0].y, 100);

    /* Both fingers jitter: still no ink, no phantom notch. */
    for (i = 0; i < 5; i++)
    {
        pt(&p[0], 0U, (uint16_t)(111 + i), 100U);
        pt(&p[1], 1U, 400U, 700U);
        gesture_feed((uint32_t)(15 + 5 * i), p, 2U);
        n = drain(ev, 16);
        moves += count_type(ev, n, GESTURE_EVT_MOVE);
        CHECK_EQ(count_type(ev, n, GESTURE_EVT_SCROLL), 0);
        CHECK_EQ(count_type(ev, n, GESTURE_EVT_PAN), 0);
    }
    CHECK_EQ(moves, 0);

    /* Buffer order swapped: the id, not the slot, decides who is primary. */
    pt(&p[0], 1U, 400U, 700U);
    pt(&p[1], 0U, 112U, 100U);
    gesture_feed(45U, p, 2U);
    n = drain(ev, 16);
    CHECK_EQ(count_type(ev, n, GESTURE_EVT_MOVE), 0);
}

/* -------------------------------------------------------------------------- */
/* 3. two fingers moving together -> one notch per 24 px                      */
/* -------------------------------------------------------------------------- */
static void test_two_finger_scroll(void)
{
    gesture_evt_t ev[16];
    gesture_point_t p[GESTURE_MAX_POINTS];
    gesture_status_t st;
    int n;
    int i;

    printf("[3] two finger scroll\n");
    gesture_init();

    pt(&p[0], 0U, 100U, 200U);
    gesture_feed(0U, p, 1U);
    (void)drain(ev, 16);

    pt(&p[0], 0U, 100U, 200U);
    pt(&p[1], 1U, 300U, 200U);
    gesture_feed(50U, p, 2U);
    (void)drain(ev, 16);

    /* Three frames x 8 px down = 24 px = exactly one notch. */
    for (i = 1; i <= 3; i++)
    {
        pt(&p[0], 0U, 100U, (uint16_t)(200 + 8 * i));
        pt(&p[1], 1U, 300U, (uint16_t)(200 + 8 * i));
        gesture_feed((uint32_t)(50 + 5 * i), p, 2U);
    }
    n = drain(ev, 16);
    CHECK_EQ(count_type(ev, n, GESTURE_EVT_SCROLL), 1);
    CHECK_EQ(count_type(ev, n, GESTURE_EVT_PAN), 0);
    CHECK_EQ(ev[0].d, 1);           /* fingers moved down -> +1 notch */

    gesture_get_status(&st);
    CHECK_EQ(st.scrolls, 1);

    /* Upwards: same amount, opposite sign. */
    for (i = 1; i <= 3; i++)
    {
        pt(&p[0], 0U, 100U, (uint16_t)(224 - 8 * i));
        pt(&p[1], 1U, 300U, (uint16_t)(224 - 8 * i));
        gesture_feed((uint32_t)(70 + 5 * i), p, 2U);
    }
    n = drain(ev, 16);
    CHECK_EQ(count_type(ev, n, GESTURE_EVT_SCROLL), 1);
    CHECK_EQ(ev[0].d, -1);
}

/* -------------------------------------------------------------------------- */
/* 4. a third finger landing must not fake a notch                            */
/* -------------------------------------------------------------------------- */
static void test_third_finger_no_phantom(void)
{
    gesture_evt_t ev[16];
    gesture_point_t p[GESTURE_MAX_POINTS];
    int n;
    int i;

    printf("[4] third finger does not create a phantom notch\n");
    gesture_init();

    pt(&p[0], 0U, 100U, 200U);
    pt(&p[1], 1U, 300U, 200U);
    gesture_feed(0U, p, 2U);
    (void)drain(ev, 16);

    /* No finger moves, but a third one lands far above both of them.  With a
     * centroid-based engine this alone produced several notches. */
    pt(&p[0], 0U, 100U, 200U);
    pt(&p[1], 1U, 300U, 200U);
    pt(&p[2], 2U, 200U, 40U);
    gesture_feed(5U, p, 3U);
    n = drain(ev, 16);
    CHECK_EQ(count_type(ev, n, GESTURE_EVT_SCROLL), 0);
    CHECK_EQ(count_type(ev, n, GESTURE_EVT_PAN), 0);

    /* Then the two original fingers really scroll: still works. */
    for (i = 1; i <= 3; i++)
    {
        pt(&p[0], 0U, 100U, (uint16_t)(200 + 8 * i));
        pt(&p[1], 1U, 300U, (uint16_t)(200 + 8 * i));
        pt(&p[2], 2U, 200U, 40U);
        gesture_feed((uint32_t)(5 + 5 * i), p, 3U);
    }
    n = drain(ev, 16);
    CHECK_EQ(count_type(ev, n, GESTURE_EVT_SCROLL), 1);
}

/* -------------------------------------------------------------------------- */
/* 5. taps                                                                     */
/* -------------------------------------------------------------------------- */
static void test_taps(void)
{
    gesture_evt_t ev[16];
    gesture_point_t p[GESTURE_MAX_POINTS];
    gesture_status_t st;
    int n;

    printf("[5] taps\n");
    gesture_init();

    pt(&p[0], 0U, 200U, 300U);
    gesture_feed(0U, p, 1U);
    gesture_feed(60U, NULL, 0U);
    n = drain(ev, 16);
    CHECK_EQ(count_type(ev, n, GESTURE_EVT_TAP), 1);

    /* Two finger tap. */
    gesture_init();
    pt(&p[0], 0U, 200U, 300U);
    pt(&p[1], 1U, 260U, 300U);
    gesture_feed(0U, p, 2U);
    gesture_feed(70U, NULL, 0U);
    n = drain(ev, 16);
    CHECK_EQ(count_type(ev, n, GESTURE_EVT_TAP2), 1);

    gesture_get_status(&st);
    CHECK_EQ(st.multi_taps, 1);
    CHECK_EQ(st.taps, 0);
}

/* -------------------------------------------------------------------------- */
/* 6. a finger lifting while another stays down must not end up as ink        */
/* -------------------------------------------------------------------------- */
static void test_finger_lift_keeps_gesture(void)
{
    gesture_evt_t ev[16];
    gesture_point_t p[GESTURE_MAX_POINTS];
    int n;

    printf("[6] lifting one of two fingers produces no ink\n");
    gesture_init();

    pt(&p[0], 0U, 100U, 100U);
    gesture_feed(0U, p, 1U);
    (void)drain(ev, 16);

    pt(&p[0], 0U, 100U, 100U);
    pt(&p[1], 1U, 300U, 300U);
    gesture_feed(10U, p, 2U);
    (void)drain(ev, 16);

    pt(&p[1], 1U, 300U, 300U);              /* first finger lifted */
    gesture_feed(20U, p, 1U);
    (void)drain(ev, 16);

    pt(&p[1], 1U, 340U, 300U);              /* survivor moves */
    gesture_feed(25U, p, 1U);
    n = drain(ev, 16);
    CHECK_EQ(count_type(ev, n, GESTURE_EVT_MOVE), 0);
    CHECK_EQ(count_type(ev, n, GESTURE_EVT_RELEASE), 0);
}

int main(void)
{
    printf("gesture multi-touch test\n");
    printf("------------------------\n");

    test_single_finger_drag();
    test_second_finger_no_jump();
    test_two_finger_scroll();
    test_third_finger_no_phantom();
    test_taps();
    test_finger_lift_keeps_gesture();

    printf("------------------------\n");
    printf("PASS %d  FAIL %d  -> %s\n", s_pass, s_fail,
           (s_fail == 0) ? "OK" : "FAILED");
    return (s_fail == 0) ? 0 : 1;
}
