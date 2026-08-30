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

/* Stack size for the UI task (words).  The UI task NO LONGER runs the MP3
 * decoder -- all decode (mp3dec_scratch_t ~16 KB) was moved to the refill task
 * (audio_player.c) so the click-to-freeze (CFSR=0x400) is structurally gone.
 * This 32 KB is generous headroom for LVGL + font load + SD/USB I/O only. */
#define UI_TASK_STACK_WORDS   8192U

/* The UI task body.  Created by main() after vTaskStartScheduler(). */
void ui_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* UI_TASK_H */
