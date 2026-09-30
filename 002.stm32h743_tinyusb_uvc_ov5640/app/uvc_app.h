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

/* Telemetry and pipeline state that used to be loose globals (uvc_ and usb_) now
 * live in the file-static s_uvc structure in uvc_app.c. They are SWD-readable,
 * e.g.  s_uvc.frames_sent, s_uvc.fps_x10, s_uvc.usb_mounted, s_uvc.state.
 * s_uvc.state packs: bit0 streaming, bit1 tx_busy, bit2 capture_busy, bit3
 * frame_ready, bit4 camera_ok, bits[11:8] cap_idx, bits[15:12] tx_idx. */

#ifdef __cplusplus
}
#endif

#endif /* UVC_APP_H */
