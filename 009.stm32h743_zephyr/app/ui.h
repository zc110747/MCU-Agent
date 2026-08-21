/**
  ******************************************************************************
  * @file    ui.h
  * @brief   LVGL info screen for the OLED demo.
  *
  *  The screen shows:
  *    - title           : "STM32H743 中文显示"
  *    - static info     : Zephyr version / LVGL version / system core clock
  *    - runtime lines   : uptime, SD font status, LED heartbeat status
  *    - bottom hint     : panel / interface description
  *
  *  Chinese text is rendered by the SD-backed GBK font (gbk_font_16); ASCII
  *  falls through to the Montserrat_14 fallback attached in lv_port_font_init().
  ******************************************************************************
  */
#ifndef _UI_H
#define _UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/**
  * @brief  Build the info screen.
  * @param  zephyr_ver   Zephyr version string (e.g. "3.7.0")
  * @param  lvgl_ver     LVGL version string (e.g. "8.4.0")
  * @param  sys_clock_hz system core clock in Hz (e.g. 480000000)
  */
void ui_show(const char *zephyr_ver, const char *lvgl_ver, uint32_t sys_clock_hz);

/** Update the SD font status line. */
void ui_set_font_status(const char *status);

/** Update the running-time line (uptime in ms). */
void ui_update_uptime(uint32_t uptime_ms);

/** Update the LED status line (heartbeat on/off). */
void ui_set_led(bool on);

#ifdef __cplusplus
}
#endif

#endif /* _UI_H */
