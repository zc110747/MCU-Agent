/**
  ******************************************************************************
  * @file    app_main.h
  * @brief   Application entry points.
  ******************************************************************************
  */
#ifndef __APP_MAIN_H
#define __APP_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/**
  * @brief  Bring up the panel, mount the SD card and draw the first screen.
  */
void application_init(void);

/**
  * @brief  Main loop body: heartbeat LED + periodic screen refresh.
  */
void application_run(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_MAIN_H */
