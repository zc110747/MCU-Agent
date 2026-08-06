/**
 * @file    app_vision.h
 * @brief   Capture -> person detection -> ST7789 overlay application.
 */
#ifndef __APP_VISION_H
#define __APP_VISION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/** Bring up the display, the camera and the neural network. */
GlobalType_t app_vision_init(void);

/** Non blocking: process one frame if the camera has produced one. */
void app_vision_loop(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_VISION_H */
