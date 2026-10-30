/* ---------------------------------------------------------------------------
 * main.c - wiring and scheduling only.
 *
 * Everything with any logic lives in its own module:
 *
 *   bsp/         clocks, pins, UART DMA, DCMI DMA, button debounce
 *   algorithms/  SAD block matching, motion filtering
 *   mouse/       input merge, accumulators, button edge queue, report build
 *   protocol/    line framing and the UART command set
 *   usb/         descriptors and the HID endpoint wrapper
 *
 * This file decides *when* things run. It does not decide *what* they do, and
 * it never touches a peripheral register.
 * -------------------------------------------------------------------------*/

#include "tusb.h"
#include "bsp.h"
#include "bsp_button.h"
#include "bsp_camera.h"
#include "bsp_uart.h"
#include "image_motion.h"
#include "motion_filter.h"
#include "mouse_core.h"
#include "mouse_input.h"
#include "uart_protocol.h"
#include "usb_hid_mouse.h"

/* LED heartbeat while no host is attached: 2 Hz is slow enough to see, fast
 * enough to prove the loop is alive on a board with no UART attached. */
#define LED_BLINK_MS    250u

typedef struct
{
    bool     usb_mounted;
    bool     camera_ready;
    uint8_t  physical_buttons;     /* last value handed to mouse_input       */
    bool     led_on;
    uint32_t led_next_ms;
    cam_status_t camera_status;    /* kept for STATUS / diagnostics          */
} app_state_t;

static app_state_t g_app =
{
    .usb_mounted = false,
    .camera_ready = false,
    .physical_buttons = 0u,
    .led_on = false,
    .led_next_ms = 0u,
    .camera_status = CAM_OK
};

/* ---------------------------------------------------------------------------
 * Input sources
 * -------------------------------------------------------------------------*/

/* Poll the debounced buttons and forward only real transitions. mouse_core
 * needs the edge, not the level: it turns each one into a report. */
static void service_buttons(uint32_t now_ms)
{
    uint8_t pressed;
    uint8_t combined_before;

    bsp_button_poll(now_ms);

    pressed = bsp_button_get_buttons();
    if (pressed == g_app.physical_buttons)
    {
        return;
    }

    g_app.physical_buttons = pressed;
    combined_before = mouse_input_get()->combined;

    mouse_input_set_physical(pressed);

    if (mouse_input_get()->combined != combined_before)
    {
        mouse_core_notify_buttons();
    }
}

/* One frame in, at most one displacement out. Keep this short: the USB stack
 * is serviced from the same loop. */
static void service_camera(void)
{
    const uint8_t *frame;
    mouse_motion_t motion;
    int32_t delta_x;
    int32_t delta_y;

    if (g_app.camera_ready == false)
    {
        return;
    }

    bsp_camera_service();

    frame = bsp_camera_acquire();
    if (frame == 0)
    {
        return;
    }

    if (image_motion_process_yuyv(frame, CAM_FRAME_W, CAM_FRAME_H, &motion) == false)
    {
        /* First frame, a flat surface, or motion beyond the search window.
         * motion_filter is not called: an invalid frame is not "zero motion"
         * and must not disturb the dead-band state. */
        return;
    }

    if (motion_filter_apply(&motion, &delta_x, &delta_y) == true)
    {
        mouse_core_add_motion(delta_x, delta_y);
    }
}

/* ---------------------------------------------------------------------------
 * USB housekeeping
 * -------------------------------------------------------------------------*/

/* A disconnect invalidates everything the host knows: queued motion is stale,
 * and any button left held would stay held after the reconnect. */
static void service_usb_state(void)
{
    bool mounted = usb_hid_mouse_mounted();

    if (mounted == g_app.usb_mounted)
    {
        return;
    }

    g_app.usb_mounted = mounted;

    if (mounted == false)
    {
        mouse_core_on_usb_disconnect();
    }
}

static void service_led(uint32_t now_ms)
{
    if (g_app.usb_mounted == true)
    {
        if (g_app.led_on == false)
        {
            board_led_write(true);
            g_app.led_on = true;
        }
        return;
    }

    if ((int32_t) (now_ms - g_app.led_next_ms) >= 0)
    {
        g_app.led_next_ms = now_ms + LED_BLINK_MS;
        g_app.led_on = (g_app.led_on == false);
        board_led_write(g_app.led_on);
    }
}

/* ---------------------------------------------------------------------------
 * Bring-up
 * -------------------------------------------------------------------------*/
static void modules_init(void)
{
    bsp_init();

    bsp_button_init();
    bsp_uart_init();

    mouse_input_init();
    mouse_core_init();
    mouse_core_set_tx(usb_hid_mouse_send);

    usb_hid_mouse_init();
    board_usb_init();
    (void) tusb_init();

    uart_protocol_init();

    image_motion_reset();
    motion_filter_init();
}

/* The camera is optional at run time. With no sensor attached the I2C probe
 * times out once (up to ~1 s, and only during boot) and the device keeps
 * working as a UART-controlled mouse - which is exactly the bring-up order the
 * project asks for: do not block USB or the command port on the optical path. */
static void camera_init(void)
{
    g_app.camera_status = bsp_camera_init();

    if (g_app.camera_status != CAM_OK)
    {
        g_app.camera_ready = false;
        return;
    }

    if (bsp_camera_start() != CAM_OK)
    {
        g_app.camera_status = CAM_ERR_DCMI;
        g_app.camera_ready = false;
        return;
    }

    g_app.camera_ready = true;
}

/* ------------------------------------------------------------------------ */
int main(void)
{
    uint32_t now;

    modules_init();
    camera_init();

    for (;;)
    {
        /* Feed the stack first: everything below can take milliseconds. */
        tud_task();

        now = board_millis();

        service_usb_state();

        bsp_uart_poll();          /* TX ring -> TDR, never blocks */

        service_buttons(now);

        uart_protocol_poll();     /* RX bytes -> lines -> mouse_core  */

        service_camera();

        /* Turn whatever accumulated into at most one USB report. */
        mouse_core_submit();

        /* Again, so a long camera pass cannot delay the stack by two frames. */
        tud_task();

        service_led(now);
    }
}
