/**
  ******************************************************************************
  * @file    ui_task.h
  * @brief   LVGL rendering thread for the STM32F429 800x400 LCD panel.
  *
  *  Brings up the FMC/8080 LCD driver, the GBK font stack (off the U-disk),
  *  LVGL and the status screen.  Runs as a FreeRTOS task so all the HAL/FMC
  *  and LVGL init (which must happen AFTER the scheduler is up, just like the
  *  USB host task) lives here instead of in main().
  ******************************************************************************
  */
#ifndef UI_TASK_H
#define UI_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

/* LVGL tick period: lv_timer_handler() is pumped from this task every
 * LVGL_TASK_PERIOD_MS.  5 ms keeps animations/refresh responsive without
 * starving the other tasks. */
#define LVGL_TASK_PERIOD_MS   5U

/* Stack size for the UI task (words).  LVGL + the font layer + the SD/USB
 * loader (HAL_SD card info, FatFs FIL objects, printf) need headroom; 6 KB. */
#define UI_TASK_STACK_WORDS   1536U

/* The UI task body.  Created by main() after vTaskStartScheduler(). */
void ui_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* UI_TASK_H */
