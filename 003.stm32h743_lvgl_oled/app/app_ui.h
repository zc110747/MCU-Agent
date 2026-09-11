/**
  ******************************************************************************
 * @file    app_ui.h
 * @brief   UI orchestrator: page array, boot/loading gate, page rotation.
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
  * @brief  Build the pages, show the boot/loading screen, and start the
  *         preload gate + page rotation.
  * @note   lv_init() and lv_port_disp_init() must already have run.
  */
void app_ui_create(void);

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
