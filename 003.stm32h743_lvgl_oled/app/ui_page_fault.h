/**
  ******************************************************************************
  * @file    ui_page_fault.h
  * @brief   ASCII-only fault page shown when the SD card / font files fail.
  ******************************************************************************
  */
#ifndef __UI_PAGE_FAULT_H
#define __UI_PAGE_FAULT_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  Replace the active screen with an ASCII-only fault page.
  * @note   The CJK glyphs live on the card that just failed, so only ASCII is
  *         drawn here (those tables are compiled into the firmware).
  */
void ui_page_fault_show(const char *line1, const char *line2, const char *line3);

#ifdef __cplusplus
}
#endif

#endif /* __UI_PAGE_FAULT_H */
