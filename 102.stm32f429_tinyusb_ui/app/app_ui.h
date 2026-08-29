/**
  ******************************************************************************
  * @file    app_ui.h
  * @brief   Multi-page LVGL screen for the STM32F429 panel.
  *
  *  Three kinds of screen exist:
  *
  *   1. Centered message screen (app_ui_show_centered)
  *      Black screen, one centered ASCII line.  Used for the boot screen
  *      ("wait for system start...") and the loader failure screen
  *      ("sdcard and usb loader failed!").  Pure ASCII, so it renders from the
  *      compiled-in ASCII tables with no font file and no filesystem at all.
  *
  *   2. Status page (page 0)
  *      The three-band device status screen: 系统初始化 / 运行信息 / 故障·消息.
  *
 *   3. Hardware information page (page 1)
 *      AP3216C (IR / ambient light / proximity) and MPU9250 (accelerometer,
 *      gyroscope, magnetometer).
 *
 *   4. Device control page (page 2)
 *      Two buttons toggle the LED (PB0, low-active) and the buzzer
 *      (PCF8574 P0, low = sound).  A status band shows the live LED / buzzer
 *      state.  PB0 is driven ONLY from this page -- led_task no longer touches
 *      it (see main.c), so the button state is not clobbered.
 *
  *  Both pages carry the same bottom navigation bar with a left and a right
  *  icon button; switching wraps around, so the two pages form a ring.
  *
  *  Every page carries Chinese text, so pages 0 and 1 are only reachable once
  *  the GBK font files have been loaded from the microSD card or the U-disk.
  *
  *  Screen teardown goes through ui_teardown(), which deletes the refresh
  *  timer BEFORE clearing the screen.  Skipping that order would leave the
  *  timer holding lv_obj_t pointers into freed objects (hard fault on the next
  *  tick).  Page switching uses ui_rebuild(), which keeps the timer alive and
  *  only re-creates the widgets.
  ******************************************************************************
  */
#ifndef APP_UI_H
#define APP_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/* Number of pages in the navigation ring. */
#define UI_PAGE_COUNT   3U

/* Build the normal status screen (page 0). */
void app_ui_create(void);

/* Wipe the screen and show a single centered line.  Keep the text pure ASCII:
 * this is the screen that must render before any font file is available. */
void app_ui_show_centered(const char *text);

/* Build the "USB / font error" fallback screen (ASCII only). */
void app_ui_show_fault(const char *line1, const char *line2, const char *line3);

/* Ask the refresh timer to re-read the USB/font state on the next tick. */
void app_ui_request_usb_refresh(void);

/* Move the navigation ring by @p delta pages (negative = left, positive =
 * right).  The index wraps, so the pages cycle in both directions. */
void app_ui_switch_page(int delta);

/* Index of the page currently on screen. */
int app_ui_page(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_UI_H */
