/**
 ******************************************************************************
 * @file    ui_page_boot.h
 * @brief   Boot / loading page (progress bar + status line).
 ******************************************************************************
 */
#ifndef __UI_PAGE_BOOT_H
#define __UI_PAGE_BOOT_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Build the boot screen (not loaded - caller does lv_screen_load). */
lv_obj_t *ui_page_boot_build(void);

/** Set the progress bar fill, 0..100. */
void ui_page_boot_set(uint8_t pct);

/** Rewrite the status line under the bar. */
void ui_page_boot_set_status(const char *status);

#ifdef __cplusplus
}
#endif

#endif /* __UI_PAGE_BOOT_H */
