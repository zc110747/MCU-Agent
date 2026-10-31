/**
 ******************************************************************************
 * @file    usb_mouse.c
 * @brief   HID mouse service: accumulator + report pump + mount state.
 *
 *  Why an accumulator
 *  ------------------
 *  The touch task produces one sample every ~10 ms and fingers jitter by a
 *  pixel or two.  Pushing a report per sample would flood the 1 ms interrupt
 *  endpoint with useless traffic; instead the deltas are scaled and summed in a
 *  Q8 accumulator, and a single report carries up to +/-127 px of it.  The
 *  sub-pixel remainder survives between reports, so slow, precise drags do not
 *  lose motion to rounding.
 *
 *  Why a dedicated task
 *  --------------------
 *  tud_task() must keep running while the host enumerates or asks for a HID
 *  report, and the UI task can block for tens of milliseconds inside an LVGL
 *  flush.  A 5 ms capped tud_task_ext() keeps the stack responsive and doubles
 *  as the report tick.
 ******************************************************************************
 */
#include "usb_mouse.h"

#include "stm32f4xx_hal.h"

#include "bsp_usb_hw.h"
#include "tusb.h"

#include "log.h"

/* ---- Shared state (guarded by taskENTER_CRITICAL) ------------------------ */
static volatile int32_t  s_acc_x_q8 = 0;    /* pending motion, 1/256 px      */
static volatile int32_t  s_acc_y_q8 = 0;
static volatile uint8_t  s_buttons  = 0;
static volatile uint8_t  s_mounted  = 0;
static volatile uint8_t  s_suspended = 0;
static volatile uint32_t s_reports  = 0;
static volatile uint32_t s_tries    = 0;   /* mouse_send() calls               */
static volatile uint32_t s_endpoint_busy = 0; /* host had not fetched the last one */
static volatile int32_t  s_total_x  = 0;
static volatile int32_t  s_total_y  = 0;

/* End-to-end latency: how long a delta waits between usb_mouse_move*() and the
 * report that carries it.  Tick based (1 ms at configTICK_RATE_HZ = 1000),
 * which is coarse but cannot underflow: the DWT cycle counter is reset by the
 * touch task during init, so a stamp taken before that turned into a 16 s
 * artefact that poisoned the average. */
static volatile TickType_t s_pend_tick = 0;
static volatile uint8_t  s_pend_valid = 0;
static volatile uint32_t s_lat_ms_sum = 0;
static volatile uint32_t s_lat_n      = 0;

/* Report-only copy of the button mask (task context, no locking needed). */
static uint8_t s_sent_buttons = 0;

static void click_pump(TickType_t now);

/* Click pulse: bits that still have to be pressed+released, and where the
 * pulse currently is (0 idle, 1 press waiting to be reported, 2 hold). */
static volatile uint8_t  s_click_mask  = 0;
static volatile uint8_t  s_click_state = 0;
static volatile TickType_t s_click_tick = 0;

/* ---- Acceleration --------------------------------------------------------- */
/* Three steps, expressed in Q8: 1.25x / 1.75x / 2.50x.  A 456 px wide pad has
 * to cross a 1920 px desktop, so fast flicks need the higher taps while slow
 * positioning stays near 1:1.
 *
 * The thresholds are a SPEED, not a per-sample distance: they are evaluated as
 * "pixels per ACC_REF_MS".  Sampling got faster over time (the pointer used to
 * be driven at ~84 Hz by the LVGL pump, it now runs at ~200 Hz from the touch
 * task), and with a per-sample threshold the same finger speed would fall out
 * of the high tap just because the samples became smaller. */
#define GAIN_SLOW_Q8     320
#define GAIN_MED_Q8      448
#define GAIN_FAST_Q8     640
#define GAIN_FAST_MAG    12        /* px per ACC_REF_MS */
#define GAIN_MED_MAG     5
#define ACC_REF_MS       10U

/* -------------------------------------------------------------------------- */
/* Report pump                                                                */
/* -------------------------------------------------------------------------- */
static void mouse_send(void)
{
    int32_t dx;
    int32_t dy;
    uint8_t btn;
    uint8_t rep[3];

    if (!tud_hid_ready())
    {
        s_tries++;
        s_endpoint_busy++;
        return;
    }

    s_tries++;

    taskENTER_CRITICAL();
    btn = s_buttons;
    dx  = s_acc_x_q8 >> 8;
    dy  = s_acc_y_q8 >> 8;
    taskEXIT_CRITICAL();

    /* Nothing changed: do not waste an interrupt IN token. */
    if ((dx == 0) && (dy == 0) && (btn == s_sent_buttons))
    {
        return;
    }

    if (dx >  127) { dx =  127; }
    if (dx < -127) { dx = -127; }
    if (dy >  127) { dy =  127; }
    if (dy < -127) { dy = -127; }

    rep[0] = btn;
    rep[1] = (uint8_t)(int8_t)dx;
    rep[2] = (uint8_t)(int8_t)dy;

    if (tud_hid_report(0, rep, sizeof(rep)))
    {
        taskENTER_CRITICAL();
        /* Subtract exactly what left the accumulator; anything that arrived
         * meanwhile stays queued for the next report. */
        s_acc_x_q8 -= (dx << 8);
        s_acc_y_q8 -= (dy << 8);
        s_reports++;
        s_total_x += dx;
        s_total_y += dy;
        if (s_pend_valid != 0U)
        {
            uint32_t waited = (uint32_t)(xTaskGetTickCount() - s_pend_tick);

            /* A gap longer than 100 ms means the host was not fetching (unplug,
             * suspend): drop it instead of letting one sample skew the mean. */
            if (waited <= 100U)
            {
                s_lat_ms_sum += waited;
                s_lat_n++;
            }
            s_pend_valid = 0U;
        }
        taskEXIT_CRITICAL();

        s_sent_buttons = btn;
    }
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */
int usb_mouse_init(void)
{
    BSP_USB_HW_Init();

    if (!tusb_init())
    {
        PRINT_LOG("[USB ] tusb_init FAILED\r\n");
        return -1;
    }

    /* tusb_init() -> dcd_init() -> dcd_int_enable() unmasks the OTG_FS vector
     * and starts answering SETUP packets.  That MUST NOT happen before the
     * scheduler runs: the ISR calls xQueueSendToBackFromISR(), and FreeRTOS
     * asserts on that from inside vTaskStartScheduler() (it fired the
     * vPortValidateInterruptPriority assert on this board before the split).
     * This function is therefore called from usb_mouse_task(), never from
     * main(). */

    /* Re-assert the NVIC priority after the port touched the controller: the
     * DWC2 port enables the vector itself, so make sure it stays inside the
     * FreeRTOS "can call FromISR" band. */
    HAL_NVIC_SetPriority(OTG_FS_IRQn,
                         configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0);

    PRINT_LOG("[USB ] OTG FS device ready (HID mouse, 3 buttons, no wheel)\r\n");
    return 0;
}

void usb_mouse_task(void *arg)
{
    (void)arg;

    /* Bring the device stack up here (scheduler running) instead of in main(). */
    while (usb_mouse_init() != 0)
    {
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }

    for (;;)
    {
        /* 2 ms, not 5: the endpoint is polled every 1 ms now, so a slower pump
         * would leave the report sitting in the accumulator for up to five
         * frames - half the latency budget spent for no benefit. */
        tud_task_ext(2U, false);

        s_mounted = (uint8_t)(tud_mounted() ? 1U : 0U);

        if (s_mounted != 0U)
        {
            click_pump(xTaskGetTickCount());
            mouse_send();
        }
    }
}

void usb_mouse_move_ex(int16_t dx, int16_t dy, uint32_t dt_ms)
{
    int32_t mag;
    int32_t gain_q8;

    if ((dx == 0) && (dy == 0))
    {
        return;
    }

    if (dt_ms == 0U)
    {
        dt_ms = 1U;
    }
    if (dt_ms > 100U)
    {
        /* A stalled loop must not look like a flick: clamp the reference to
         * 100 ms so a 2 px delta after a hiccup stays in the slow tap. */
        dt_ms = 100U;
    }

    mag = ((dx < 0) ? -(int32_t)dx : (int32_t)dx)
        + ((dy < 0) ? -(int32_t)dy : (int32_t)dy);
    mag = (mag * (int32_t)ACC_REF_MS) / (int32_t)dt_ms;

    if (mag >= GAIN_FAST_MAG)
    {
        gain_q8 = GAIN_FAST_Q8;
    }
    else if (mag >= GAIN_MED_MAG)
    {
        gain_q8 = GAIN_MED_Q8;
    }
    else
    {
        gain_q8 = GAIN_SLOW_Q8;
    }

    taskENTER_CRITICAL();
    /* Start the latency clock only when motion begins, i.e. when nothing was
     * already queued.  A remainder keeps the original stamp so the measurement
     * covers the whole wait, not just the last sample. */
    if (s_pend_valid == 0U)
    {
        s_pend_tick  = xTaskGetTickCount();
        s_pend_valid = 1U;
    }
    s_acc_x_q8 += (int32_t)dx * gain_q8;
    s_acc_y_q8 += (int32_t)dy * gain_q8;
    taskEXIT_CRITICAL();
}

void usb_mouse_move(int16_t dx, int16_t dy)
{
    usb_mouse_move_ex(dx, dy, ACC_REF_MS);
}

void usb_mouse_set_button(uint8_t mask, int pressed)
{
    taskENTER_CRITICAL();
    if (pressed != 0)
    {
        s_buttons |= mask;
    }
    else
    {
        s_buttons &= (uint8_t)~mask;
    }
    taskEXIT_CRITICAL();
}

void usb_mouse_click(uint8_t mask)
{
    if (mask == 0U)
    {
        return;
    }

    taskENTER_CRITICAL();
    s_click_mask |= mask;
    taskEXIT_CRITICAL();
}

/**
 * @brief  Drive one pending click pulse.  Called from the USB task right before
 *         mouse_send() so the press leaves in the same 2 ms slot.
 *
 *  A press is only released once the report carrying it has been accepted by
 *  the host (mouse_send() sets s_sent_buttons on success).  If the host never
 *  fetches it - unplugged, suspended - the pulse gives up after 200 ms rather
 *  than leaving a button stuck down.
 */
static void click_pump(TickType_t now)
{
    uint8_t mask;

    taskENTER_CRITICAL();
    mask = s_click_mask;
    taskEXIT_CRITICAL();

    switch (s_click_state)
    {
    case 0U:                            /* idle -> assert the press */
        if (mask != 0U)
        {
            taskENTER_CRITICAL();
            s_buttons |= mask;
            taskEXIT_CRITICAL();

            s_click_state = 1U;
            s_click_tick  = now;
        }
        break;

    case 1U:                            /* waiting for the press report */
        if (((s_sent_buttons & mask) == mask) || ((TickType_t)(now - s_click_tick) > 200U))
        {
            s_click_state = 2U;
            s_click_tick  = now;
        }
        break;

    default:                            /* holding -> release */
        if ((TickType_t)(now - s_click_tick) >= USB_MOUSE_CLICK_MS)
        {
            taskENTER_CRITICAL();
            s_buttons    &= (uint8_t)~mask;
            s_click_mask &= (uint8_t)~mask;
            taskEXIT_CRITICAL();

            s_click_state = 0U;
        }
        break;
    }
}

void usb_mouse_release_all(void)
{
    taskENTER_CRITICAL();
    s_buttons     = 0U;
    s_click_mask  = 0U;
    s_click_state = 0U;
    taskEXIT_CRITICAL();
}

uint8_t usb_mouse_buttons(void)
{
    return s_buttons;
}

int usb_mouse_mounted(void)
{
    return (s_mounted != 0U) ? 1 : 0;
}

void usb_mouse_get_status(usb_mouse_status_t *out)
{
    if (out == NULL)
    {
        return;
    }

    taskENTER_CRITICAL();
    out->mounted   = s_mounted;
    out->suspended = s_suspended;
    out->buttons   = s_buttons;
    out->reports   = s_reports;
    out->tries     = s_tries;
    out->endpoint_busy = s_endpoint_busy;
    out->lat_avg_ms = (s_lat_n != 0U) ? (s_lat_ms_sum / s_lat_n) : 0U;
    out->x_total   = s_total_x;
    out->y_total   = s_total_y;
    taskEXIT_CRITICAL();
}

/* -------------------------------------------------------------------------- */
/* TinyUSB device callbacks                                                   */
/* -------------------------------------------------------------------------- */
void tud_mount_cb(void)
{
    s_mounted = 1U;
    PRINT_LOG("[USB ] mounted by host\r\n");
}

void tud_umount_cb(void)
{
    s_mounted   = 0U;
    s_suspended = 0U;

    /* Drop whatever was queued: after a re-plug the stale motion would show up
     * as one violent cursor jump. */
    taskENTER_CRITICAL();
    s_buttons  = 0U;
    s_acc_x_q8 = 0;
    s_acc_y_q8 = 0;
    taskEXIT_CRITICAL();

    s_sent_buttons = 0U;
    PRINT_LOG("[USB ] unmounted\r\n");
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
    s_suspended = 1U;
}

void tud_resume_cb(void)
{
    s_suspended = 0U;
}
