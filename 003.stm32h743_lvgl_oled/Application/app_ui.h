/**
  ******************************************************************************
  * @file    app_ui.h
  * @brief   LVGL screen: clock, SD capacity and board info, in Chinese.
  ******************************************************************************
  */
#ifndef __APP_UI_H
#define __APP_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "lvgl.h"

/**
  * @brief  Build the main screen and start the 1 s refresh timer.
  * @note   lv_init() and lv_port_disp_init() must already have run.
  */
void app_ui_create(void);

/**
  * @brief  Replace the screen with an ASCII-only fault page.
  *
  * Used when the SD card or the font files are missing: the CJK glyphs live on
  * that card, so anything Chinese would come out blank.  ASCII still works -
  * those tables are compiled into the firmware.
  */
void app_ui_show_fault(const char *line1, const char *line2, const char *line3);

/**
  * @brief  Force an SD capacity re-read on the next refresh tick.
  */
void app_ui_request_sd_refresh(void);

/**
  * @brief  If the 5 s page-rotation timer has fired, return the next screen.
  * @param  out_screen  set to the target screen object on a hit.
  * @param  out_index   set to 0 (info panel) or 1 (font page) on a hit.
  * @return 1 if a switch is pending (and *out_screen is valid), else 0.
  * @note   Caller should lv_scr_load() then lv_timer_handler() so the
  *         latency of the redraw (incl. Chinese glyph rasterisation)
  *         can be measured around that single call.
  */
uint8_t app_ui_take_switch(lv_obj_t **out_screen, int *out_index);

#ifdef __cplusplus
}
#endif

#endif /* __APP_UI_H */
