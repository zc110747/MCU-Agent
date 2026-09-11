/**
  ******************************************************************************
  * @file    ui_page_info.h
  * @brief   Main info panel: clock, SD capacity and board info, in Chinese.
  ******************************************************************************
  */
#ifndef __UI_PAGE_INFO_H
#define __UI_PAGE_INFO_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Build the info-panel screen + its 1 Hz refresh timer (off-screen). */
lv_obj_t *ui_page_info_build(void);

/** Force an SD capacity re-read on the next refresh tick. */
void ui_page_info_request_sd_refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* __UI_PAGE_INFO_H */
