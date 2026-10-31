/**
 ******************************************************************************
 * @file    usb_mouse.h
 * @brief   HID mouse service on top of TinyUSB (relative pointer, 3 buttons).
 *
 *  Model
 *  -----
 *  The panel is used as a trackpad: relative finger motion is accumulated in a
 *  Q8 (1/256 px) accumulator with a small three-step acceleration curve, and a
 *  dedicated task pushes one 3-byte report whenever something changed.  Both
 *  the touch side and the UI side may call the setters from any task.
 *
 *  Report (see app/usb_descriptors.c):
 *      byte 0  bit0 = left, bit1 = right, bit2 = middle
 *      byte 1  X  (relative, -127..127)
 *      byte 2  Y  (relative, -127..127)
 *
 *  No wheel yet, by design.
 ******************************************************************************
 */
#ifndef __USB_MOUSE_H__
#define __USB_MOUSE_H__

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

/* HID button bits as they appear in byte 0 of the report.  Note that HID
 * button *numbers* are 1=left, 2=right, 3=middle, hence the bit order. */
#define USB_MOUSE_BTN_LEFT     0x01U
#define USB_MOUSE_BTN_RIGHT    0x02U
#define USB_MOUSE_BTN_MIDDLE   0x04U

/* Task sizing.  TinyUSB + OSAL + the report path need a bit more than the
 * touch task; 1 KB is comfortable and costs nothing (SDRAM heap). */
#define USB_MOUSE_TASK_STACK_WORDS   1024U

/* Highest of the application tasks: enumeration must not be starved by a long
 * LVGL render pass. */
#define USB_MOUSE_TASK_PRIO          (tskIDLE_PRIORITY + 5U)

typedef struct
{
    uint8_t  mounted;       /* 1 while the host has us configured          */
    uint8_t  suspended;     /* 1 while the bus is suspended                */
    uint8_t  buttons;       /* current button bit mask                      */
    uint32_t reports;       /* reports accepted by the host                 */
    uint32_t tries;         /* mouse_send() invocations                     */
    uint32_t endpoint_busy; /* ... of which the EP was still owned by host  */
    uint32_t lat_avg_ms;    /* mean (queue -> report) latency, milliseconds */
    int32_t  x_total;       /* signed pixel sum sent since boot             */
    int32_t  y_total;
} usb_mouse_status_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Board bring-up (clock/pins/NVIC) + tusb_init().
 *         Must run after vPortDefineHeapRegions() (the OSAL allocates queues)
 *         and before usb_mouse_task() is created.
 * @retval 0 on success, -1 when the controller did not init.
 */
int usb_mouse_init(void);

/** Task body: pumps tud_task() and emits pending reports (~200 Hz when idle). */
void usb_mouse_task(void *arg);

/**
 * @brief  Queue a relative move in pixels (trackpad delta, not screen delta).
 *         Values are scaled by the acceleration curve and accumulated, so a
 *         caller may pass raw sample-to-sample deltas.  This variant assumes a
 *         10 ms reference period; use usb_mouse_move_ex() when the caller knows
 *         how long the sample took.
 */
void usb_mouse_move(int16_t dx, int16_t dy);

/**
 * @brief  Same as usb_mouse_move(), but the acceleration curve is normalised
 *         by @p dt_ms so the selected gain depends on finger SPEED rather than
 *         on how often the caller happens to sample.
 */
void usb_mouse_move_ex(int16_t dx, int16_t dy, uint32_t dt_ms);

/** Press (pressed != 0) or release one or more buttons of @p mask. */
void usb_mouse_set_button(uint8_t mask, int pressed);

/**
 * @brief  Emit one self-timed click of @p mask (press, hold USB_MOUSE_CLICK_MS,
 *         release).  Safe from any task; the pulse is driven by the USB task.
 *
 *  Why this is not just set_button(1) + set_button(0): mouse_send() compares
 *  the current mask against the one it last *sent*, and the pump only runs once
 *  every 2 ms.  A click that is pressed and released inside one interval has a
 *  net delta of zero, so nothing is transmitted and the host never sees it.
 *  The pulse keeps the press asserted until a report carrying it has actually
 *  been accepted by the host.
 */
void usb_mouse_click(uint8_t mask);

/* How long a click stays asserted.  Long enough that a 1 ms host polling
 * interval cannot miss it, short enough to feel instantaneous. */
#define USB_MOUSE_CLICK_MS      40U

/** Release every button (used the UI is torn down / on unmount). */
void usb_mouse_release_all(void);

/** Current button mask. */
uint8_t usb_mouse_buttons(void);

/** @retval 1 while the host keeps the device configured. */
int usb_mouse_mounted(void);

/** Snapshot of the counters / state (safe from any task). */
void usb_mouse_get_status(usb_mouse_status_t *out);

#ifdef __cplusplus
}
#endif

#endif /* __USB_MOUSE_H__ */
