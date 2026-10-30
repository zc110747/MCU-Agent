/* ---------------------------------------------------------------------------
 * mouse_core.c - accumulator + button edge FIFO + transmit pacing.
 * See mouse_core.h for the three invariants this file implements.
 * -------------------------------------------------------------------------*/

#include "mouse_core.h"
#include "mouse_input.h"
#include "bsp.h"

/* Sentinel meaning "the host has no idea what our button state is" - it can
 * never equal a real 3-bit mask, so the next report is always transmitted. */
#define BUTTONS_UNKNOWN   0xFFu

typedef struct
{
    uint8_t buf[MOUSE_BTN_QUEUE_DEPTH];
    uint8_t head;              /* index of the oldest entry                 */
    uint8_t count;             /* number of valid entries                   */
} btn_queue_t;

typedef struct
{
    int32_t  x;                /* pending relative motion, counts           */
    int32_t  y;
    int32_t  wheel;
    int32_t  pan;

    btn_queue_t btn_q;

    uint8_t  buttons_sent;     /* last value the sink accepted, or UNKNOWN  */
    uint8_t  buttons_seen;     /* combined state at the previous submit     */

    uint32_t reports_sent;
    uint32_t reports_failed;
    uint32_t button_events;
    uint32_t button_overflow;
    uint32_t pending_saturations;

    uint32_t last_tx_ms;

    mouse_core_tx_fn tx;
} mouse_core_state_t;

static mouse_core_state_t g_core =
{
    .x = 0,
    .y = 0,
    .wheel = 0,
    .pan = 0,
    .btn_q = { .buf = { 0u }, .head = 0u, .count = 0u },
    .buttons_sent = BUTTONS_UNKNOWN,
    .buttons_seen = 0u,
    .reports_sent = 0u,
    .reports_failed = 0u,
    .button_events = 0u,
    .button_overflow = 0u,
    .pending_saturations = 0u,
    .last_tx_ms = 0u,
    .tx = 0
};

/* ---------------------------------------------------------------------------
 * Button edge FIFO
 * -------------------------------------------------------------------------*/

/* Push a snapshot. Identical neighbours collapse, so a level that flickers
 * between two identical reports does not fill the FIFO. On overflow the oldest
 * entry is dropped: the newest state must always survive, otherwise the host
 * could be left with a button stuck down. */
static void btn_queue_push(uint8_t buttons)
{
    uint8_t tail;
    uint8_t last;

    if (g_core.btn_q.count > 0u)
    {
        tail = (uint8_t) ((g_core.btn_q.head + g_core.btn_q.count - 1u) %
                          MOUSE_BTN_QUEUE_DEPTH);
        last = g_core.btn_q.buf[tail];
        if (last == buttons)
        {
            return;
        }
    }

    if (g_core.btn_q.count >= MOUSE_BTN_QUEUE_DEPTH)
    {
        g_core.btn_q.head = (uint8_t) ((g_core.btn_q.head + 1u) %
                                       MOUSE_BTN_QUEUE_DEPTH);
        g_core.btn_q.count--;
        g_core.button_overflow++;
    }

    g_core.btn_q.buf[(g_core.btn_q.head + g_core.btn_q.count) %
                     MOUSE_BTN_QUEUE_DEPTH] = buttons;
    g_core.btn_q.count++;
    g_core.button_events++;
}

static uint8_t btn_queue_peek(void)
{
    return g_core.btn_q.buf[g_core.btn_q.head];
}

static void btn_queue_pop(void)
{
    if (g_core.btn_q.count == 0u)
    {
        return;
    }

    g_core.btn_q.head = (uint8_t) ((g_core.btn_q.head + 1u) % MOUSE_BTN_QUEUE_DEPTH);
    g_core.btn_q.count--;
}

/* ---------------------------------------------------------------------------
 * Accumulators
 * -------------------------------------------------------------------------*/

static int32_t accum_add(int32_t accumulator, int32_t delta)
{
    int32_t value = accumulator + delta;

    if (value > MOUSE_PENDING_LIMIT)
    {
        value = MOUSE_PENDING_LIMIT;
        g_core.pending_saturations++;
    }
    else if (value < -MOUSE_PENDING_LIMIT)
    {
        value = -MOUSE_PENDING_LIMIT;
        g_core.pending_saturations++;
    }
    else
    {
        /* in range - nothing to do */
    }

    return value;
}

static int16_t clamp_to_axis(int32_t value)
{
    if (value > MOUSE_REPORT_AXIS_MAX)
    {
        return (int16_t) MOUSE_REPORT_AXIS_MAX;
    }
    if (value < -MOUSE_REPORT_AXIS_MAX)
    {
        return (int16_t) -MOUSE_REPORT_AXIS_MAX;
    }
    return (int16_t) value;
}

static bool has_pending(void)
{
    return (g_core.x != 0) || (g_core.y != 0) ||
           (g_core.wheel != 0) || (g_core.pan != 0);
}

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/

void mouse_core_init(void)
{
    g_core.x = 0;
    g_core.y = 0;
    g_core.wheel = 0;
    g_core.pan = 0;

    g_core.btn_q.head  = 0u;
    g_core.btn_q.count = 0u;

    g_core.buttons_sent = BUTTONS_UNKNOWN;
    g_core.buttons_seen = 0u;

    g_core.reports_sent      = 0u;
    g_core.reports_failed    = 0u;
    g_core.button_events     = 0u;
    g_core.button_overflow   = 0u;
    g_core.pending_saturations = 0u;

    g_core.last_tx_ms = 0u;
    g_core.tx = 0;
}

void mouse_core_set_tx(mouse_core_tx_fn tx)
{
    g_core.tx = tx;
}

void mouse_core_add_motion(int32_t delta_x, int32_t delta_y)
{
    g_core.x = accum_add(g_core.x, delta_x);
    g_core.y = accum_add(g_core.y, delta_y);
}

void mouse_core_add_wheel(int32_t delta_wheel)
{
    g_core.wheel = accum_add(g_core.wheel, delta_wheel);
}

void mouse_core_add_pan(int32_t delta_pan)
{
    g_core.pan = accum_add(g_core.pan, delta_pan);
}

/* Called by the two input drivers the instant a combined button state changes.
 * Doing it here - at the edge - instead of diffing levels inside submit() is
 * what makes a press that is released within one endpoint-busy window survive
 * as two distinct reports. */
void mouse_core_notify_buttons(void)
{
    const mouse_input_t *input = mouse_input_get();

    if (input->combined == g_core.buttons_seen)
    {
        return;
    }

    g_core.buttons_seen = input->combined;
    btn_queue_push(input->combined);
}

void mouse_core_submit(void)
{
    mouse_report_t report;
    const mouse_input_t *input;
    uint32_t now;
    bool queued;

    if (g_core.tx == 0)
    {
        return;
    }

    /* Belt and braces: if a caller changed the input state without notifying,
     * the diff still catches it, just one main-loop pass later. */
    input = mouse_input_get();
    if (input->combined != g_core.buttons_seen)
    {
        mouse_core_notify_buttons();
    }

    queued = (g_core.btn_q.count > 0u);

    if ((queued == false) && (has_pending() == false))
    {
        return;
    }

    now = board_millis();
    if ((g_core.reports_sent != 0u) &&
        ((now - g_core.last_tx_ms) < MOUSE_TX_INTERVAL_MS))
    {
        return;
    }

    mouse_report_clear(&report);

    /* A queued edge always wins, even if it happens to equal buttons_sent:
     * it is an event the host has not been told about yet. */
    if (queued)
    {
        report.buttons = btn_queue_peek();
    }
    else
    {
        report.buttons = input->combined;
    }

    report.x     = clamp_to_axis(g_core.x);
    report.y     = clamp_to_axis(g_core.y);
    report.wheel = (int8_t) clamp_to_axis(g_core.wheel);
    report.pan   = (int8_t) clamp_to_axis(g_core.pan);

    /* Nothing new for the host to see. Only reachable when no edge is queued. */
    if ((queued == false) && (report.buttons == g_core.buttons_sent) &&
        mouse_report_is_idle(&report))
    {
        return;
    }

    if (g_core.tx(&report) == false)
    {
        /* Endpoint busy or bus unconfigured: keep every pending byte. */
        g_core.reports_failed++;
        return;
    }

    g_core.last_tx_ms = now;
    g_core.reports_sent++;
    g_core.buttons_sent = report.buttons;

    g_core.x     -= (int32_t) report.x;
    g_core.y     -= (int32_t) report.y;
    g_core.wheel -= (int32_t) report.wheel;
    g_core.pan   -= (int32_t) report.pan;

    if (queued)
    {
        btn_queue_pop();
    }
}

void mouse_core_flush(void)
{
    uint32_t guard = 0u;

    /* Each iteration drains at most 127 counts per axis, so the loop is bounded
     * by the accumulator limit; the guard only protects against a sink that
     * reports success forever without the state actually moving. */
    while (((g_core.btn_q.count > 0u) || has_pending()) &&
           (guard < (4u * (uint32_t) MOUSE_PENDING_LIMIT)))
    {
        uint32_t sent_before = g_core.reports_sent;

        g_core.last_tx_ms = board_millis() - MOUSE_TX_INTERVAL_MS;
        mouse_core_submit();

        if (g_core.reports_sent == sent_before)
        {
            break;        /* sink refused - stop, the state is still queued */
        }
        guard++;
    }
}

void mouse_core_on_usb_disconnect(void)
{
    /* Motion measured before the unplug is stale: replaying it after the
     * reconnect would fling the cursor across the screen. */
    g_core.x = 0;
    g_core.y = 0;
    g_core.wheel = 0;
    g_core.pan = 0;

    g_core.btn_q.head  = 0u;
    g_core.btn_q.count = 0u;

    /* The host forgot everything while we were unplugged, so the next report
     * must carry the live state unconditionally. */
    g_core.buttons_sent = BUTTONS_UNKNOWN;
}

void mouse_core_get_status(mouse_core_status_t *status)
{
    if (status == 0)
    {
        return;
    }

    status->reports_sent       = g_core.reports_sent;
    status->reports_failed     = g_core.reports_failed;
    status->button_events      = g_core.button_events;
    status->button_overflow    = g_core.button_overflow;
    status->pending_saturations = g_core.pending_saturations;
    status->pending_x          = g_core.x;
    status->pending_y          = g_core.y;
    status->pending_wheel      = g_core.wheel;
    status->pending_pan        = g_core.pan;
    status->buttons_combined   = mouse_input_get()->combined;
    status->buttons_sent       = g_core.buttons_sent;
    status->button_queue_len   = g_core.btn_q.count;
}
