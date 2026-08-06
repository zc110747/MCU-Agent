/**
 ******************************************************************************
 * @file    uvc_app.h
 * @brief   UVC streaming application: ties the DCMI capture front-end to the
 *          TinyUSB video class driver.
 ******************************************************************************
 */

#ifndef UVC_APP_H
#define UVC_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* @param camera_ok  false -> stream a synthetic colour-bar test pattern so the
 *                   USB side can still be validated without a working sensor. */
void uvc_app_init(bool camera_ok);

/* Pump the capture / transmit state machine. Call from the main loop. */
void uvc_app_task(void);

/* True while the host has selected alternate setting 1 and is receiving data. */
bool uvc_app_is_streaming(void);

/* Debug counters, handy to watch in the cortex-debug variables pane. */
extern volatile uint32_t uvc_frames_sent;
extern volatile uint32_t uvc_frames_dropped;

/* USB lifecycle telemetry (see uvc_app.c) */
extern volatile uint32_t usb_mounted;
extern volatile uint32_t usb_mount_count;
extern volatile uint32_t usb_suspend_count;
extern volatile uint32_t usb_commit_count;

/* Pipeline telemetry */
extern volatile uint32_t uvc_xfer_started;
extern volatile uint32_t uvc_xfer_rejected;
extern volatile uint32_t uvc_tx_timeouts;
extern volatile uint32_t uvc_cap_timeouts;
extern volatile uint32_t uvc_stream_poll_true;
extern volatile uint32_t uvc_stream_poll_false;

/* Packed pipeline state, sampled at the top of every uvc_app_task():
 *   bit0 streaming  bit1 tx_busy  bit2 capture_busy  bit3 frame_ready
 *   bit4 camera_ok  bits[11:8] cap_idx  bits[15:12] tx_idx                  */
extern volatile uint32_t uvc_state;

#ifdef __cplusplus
}
#endif

#endif /* UVC_APP_H */
