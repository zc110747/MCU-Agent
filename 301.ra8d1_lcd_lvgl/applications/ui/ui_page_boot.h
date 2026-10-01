/**
 ******************************************************************************
 * @file    ui_page_boot.h
 * @brief   Boot splash with an animated action bar (Phase 8).
 *
 *  Shown the instant the LVGL thread is up, replacing the static colour-bar
 *  pattern that used to sit on the panel during bring-up.  A progress bar
 *  advances step by step while the current action (RTC / panel / DSI / LVGL)
 *  is named underneath, then the caller fades to the main screen.
 ******************************************************************************
 */
#ifndef __UI_PAGE_BOOT_H
#define __UI_PAGE_BOOT_H

#include "lvgl.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Build the boot screen and return it (caller loads it). */
lv_obj_t *ui_page_boot_build(void);

/** Advance the action bar to `pct` (0..100) and set the step caption.
 *  Called from the boot sequencing code, not from a timer. */
void ui_page_boot_set(int32_t pct, const char *caption);

/** True once the bar has reached 100% (the boot sequencer then swaps to the
 *  main screen). */
bool ui_page_boot_done(void);

#ifdef __cplusplus
}
#endif

#endif /* __UI_PAGE_BOOT_H */
