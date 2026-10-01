/**
 ******************************************************************************
 * @file    app_ui.h
 * @brief   UI orchestrator: page array, boot gate, msh-driven navigation.
 *
 *  Ported 1:1 from the reference project 003.stm32h743_lvgl_oled (same page
 *  array + boot-gate pattern), adapted for this board:
 *    - no physical buttons -> the cursor is driven over the RT-Thread msh
 *      console (see app_ui_* entry points used by main.c's "menu" command);
 *    - no font preload queue -> the boot bar tracks a fixed minimum dwell.
 ******************************************************************************
 */
#ifndef __APP_UI_H
#define __APP_UI_H

#include "lvgl.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Page ids (index into the page array). */
#define APP_UI_BOOT 0U
#define APP_UI_MENU 1U
#define APP_UI_INFO 2U
#define APP_UI_PAGE_COUNT 3U

/** Menu action ids returned by the menu page. */
#define APP_UI_ACT_TEST 1U
#define APP_UI_ACT_FILL 2U
#define APP_UI_ACT_DEMO 3U
#define APP_UI_ACT_INFO 4U

/** Build every page, show the boot page, and start the boot gate.
 *  Must be called from the LVGL thread (uses LVGL APIs). */
void app_ui_create(void);

/** Show a page by id, running the LVGL calls inline (LVGL thread only). */
void app_ui_show(uint8_t page_id);

/** Current visible page id. */
uint8_t app_ui_current(void);

/* ---- navigation, served inside the LVGL thread ----------------------------
   The msh command only posts a request; the actual LVGL calls happen in
   app_ui_service() so no LVGL object is touched off-thread. */
void app_ui_req_up(void);
void app_ui_req_down(void);
void app_ui_req_enter(void);
void app_ui_req_select(uint8_t index);
void app_ui_req_back(void);

/** Drain pending requests. Called from the LVGL thread each loop. */
void app_ui_service(void);

/** Read-back helpers for the msh command (volatile snapshot only). */
uint8_t     app_ui_menu_index(void);
uint8_t     app_ui_menu_count(void);
const char *app_ui_menu_label(uint8_t index);
uint8_t     app_ui_last_action(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_UI_H */
