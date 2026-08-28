/**
  ******************************************************************************
  * @file    app_ui.h
  * @brief   LVGL screen for the STM32F429 800x400 panel.
  *
  *  Minimal dark UI (no color accents): a header plus a few status lines that
  *  report the USB disk state, the GBK font status, the MCU frequency, the
  *  uptime and the LVGL glyph cache counters.  Adapted from
  *  003.stm32h743_lvgl_oled/Application/app_ui.c (RTC/SD removed).
  ******************************************************************************
  */
#ifndef APP_UI_H
#define APP_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/* Build the normal status screen. */
void app_ui_create(void);

/* Wipe the screen and show a single centered line.  Keep the text pure ASCII:
 * this is the screen that must render before any font file is available. */
void app_ui_show_centered(const char *text);

/* Build the "USB / font error" fallback screen (ASCII only). */
void app_ui_show_fault(const char *line1, const char *line2, const char *line3);

/* Ask the 1 Hz refresh timer to re-read the USB/font state on the next tick. */
void app_ui_request_usb_refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_UI_H */
