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

/** Reported by the "status" console command and the About page. */
#define APP_FW_NAME     "H743-NES"
#define APP_FW_VERSION  "1.0.0"

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
