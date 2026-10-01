/**
 ******************************************************************************
 * @file    ui_page_main.h
 * @brief   The single application screen - see ui_page_main.c.
 ******************************************************************************
 */
#ifndef __UI_PAGE_MAIN_H
#define __UI_PAGE_MAIN_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build the one screen: board name, live hardware telemetry and a
 *        hardware-RTC clock refreshed at 1 Hz.
 * @return the created top-level screen.
 */
lv_obj_t *ui_page_main_build(void);

#ifdef __cplusplus
}
#endif

#endif /* __UI_PAGE_MAIN_H */
