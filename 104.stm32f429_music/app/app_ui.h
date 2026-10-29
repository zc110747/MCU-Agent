/**
  ******************************************************************************
  * @file    app_ui.h
  * @brief   LVGL screen: track name, elapsed bar, transport state.
  ******************************************************************************
  */
#ifndef APP_UI_H
#define APP_UI_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  Initialise LVGL, register the panel and build the screen.
  *         Must run after the SDRAM heap and FreeRTOS heap are up (LVGL's
  *         working memory and the draw buffer live in external SDRAM).
  */
void app_ui_init(void);

/**
  * @brief  Create the UI task: LVGL housekeeping + key scanning + widget refresh.
  */
void app_ui_start(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_UI_H */
