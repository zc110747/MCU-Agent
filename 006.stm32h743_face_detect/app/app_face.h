/**
 * @file    app_face.h
 * @brief   Camera -> face detection -> 240x240 panel application layer.
 */
#ifndef __APP_FACE_H
#define __APP_FACE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/**
 * @brief  Bring up panel, detector and camera, in that order.
 *
 * The panel comes first so that any later failure can be reported on screen
 * instead of only on the UART.
 */
GlobalType_t app_face_init(void);

/** One pass of the pipeline; call it back to back from main(). */
void app_face_loop(void);

#ifdef __cplusplus
}
#endif
#endif /* __APP_FACE_H */
