/**
 ******************************************************************************
 * @file    ui_page_menu.h
 * @brief   Main menu page: a vertical list with a keyboard/msh-driven cursor.
 *
 *  This board has no physical buttons, so the cursor is driven over the RT-Thread
 *  msh console ("menu up" / "menu down" / "menu enter" / "menu select N").
 ******************************************************************************
 */
#ifndef __UI_PAGE_MENU_H
#define __UI_PAGE_MENU_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Build the menu screen (not loaded - caller does lv_screen_load). */
lv_obj_t *ui_page_menu_build(void);

/** Move the cursor by +1 / -1, clamped at both ends. */
void ui_page_menu_move(int delta);

/** Move the cursor straight to an index (clamped). */
void ui_page_menu_select(uint8_t index);

/** Activate the current row. Returns the row index that was entered. */
uint8_t ui_page_menu_enter(void);

/** Current cursor row. */
uint8_t ui_page_menu_index(void);

/** Row label (for the msh read-back). */
const char *ui_page_menu_label(uint8_t index);

/** Number of rows. */
uint8_t ui_page_menu_count(void);

/**
 * Register the click handler used by an optional touch/encoder input device.
 * Called once after the screen is built.
 */
void ui_page_menu_set_input(lv_indev_t *indev);

#ifdef __cplusplus
}
#endif

#endif /* __UI_PAGE_MENU_H */
