/**
  ******************************************************************************
  * @file    ui_page_boot.h
  * @brief   Boot / font-preload loading page: "Waiting..." + dynamic bar.
  ******************************************************************************
  */
#ifndef __UI_PAGE_BOOT_H
#define __UI_PAGE_BOOT_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Build the boot/loading screen (off-screen; caller loads it). */
lv_obj_t *ui_page_boot_build(void);

/** Drive the progress bar, 0..100. */
void ui_page_boot_set(uint8_t pct);

/** Replace the status line (e.g. "字库预加载中..." vs "系统启动中..."). */
void ui_page_boot_set_status(const char *status);

#ifdef __cplusplus
}
#endif

#endif /* __UI_PAGE_BOOT_H */
