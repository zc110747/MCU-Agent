/**
 ******************************************************************************
 * @file    ui_task.h
 * @brief   LVGL pump: display bring-up, storage probe, screen construction.
 ******************************************************************************
 */
#ifndef __UI_TASK_H__
#define __UI_TASK_H__

#include "FreeRTOS.h"

/* Stack in words.  LVGL renders in this task, so it needs more than the input
 * side (drawing code pushes a few hundred bytes of locals). */
#define UI_TASK_STACK_WORDS      2048U

/* Below the touch task: a press must be published before the next LVGL read. */
#define UI_TASK_PRIO             (tskIDLE_PRIORITY + 2U)

/* LVGL timer period.  The panel is polled (no DMA), so 5 ms keeps the render
 * pump responsive without starving anything. */
#define UI_TICK_MS               5U

/* If the card is missing, the main screen is built anyway after this long so
 * the board is usable (keys + drawing) while the probe keeps retrying. */
#define UI_BOOT_TIMEOUT_MS       10000U

/* Storage probe period while waiting for a card. */
#define UI_PROBE_MS              500U

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  UI thread body: panel init -> LVGL init -> SD/font probe -> main
  *         screen -> endless lv_timer_handler() + gesture drain loop.
  */
void ui_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* __UI_TASK_H__ */
