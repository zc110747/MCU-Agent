/**
 ******************************************************************************
 * @file    ui_page_info.h
 * @brief   System info page (CPU, clocks, memory, display path).
 ******************************************************************************
 */
#ifndef __UI_PAGE_INFO_H
#define __UI_PAGE_INFO_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Build the system info screen (not loaded - caller does lv_screen_load). */
lv_obj_t *ui_page_info_build(void);

#ifdef __cplusplus
}
#endif

#endif /* __UI_PAGE_INFO_H */
