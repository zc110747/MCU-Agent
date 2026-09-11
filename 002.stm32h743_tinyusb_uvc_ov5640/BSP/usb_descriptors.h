/**
 ******************************************************************************
 * @file    usb_descriptors.h
 ******************************************************************************
 */

#ifndef USB_DESCRIPTORS_H
#define USB_DESCRIPTORS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h" /* FRAME_WIDTH / FRAME_HEIGHT / FRAME_RATE */

enum {
  ITF_NUM_VIDEO_CONTROL = 0,
  ITF_NUM_VIDEO_STREAMING,
  ITF_NUM_TOTAL
};

#define EPNUM_VIDEO_IN  0x81

#ifdef __cplusplus
}
#endif

#endif /* USB_DESCRIPTORS_H */
