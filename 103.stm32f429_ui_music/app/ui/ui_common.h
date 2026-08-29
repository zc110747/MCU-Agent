/**
  ******************************************************************************
  * @file    app/ui/ui_common.h
  * @brief   Shared declarations between the UI core (app_ui.c) and the per-page
  *          files (app/ui/page_*.c).
  *
  *  Everything a page builder needs from the framework lives here: the geometry
  *  macros, the dark palette, the page enum, the shared widget-handle struct and
  *  the global state it reads, the shared widget helpers, and the prototypes of
  *  the page builders / refreshers that the core dispatch and 2 Hz tick call.
  *
  *  Nothing in this header allocates storage: macros, typedefs, an enum, a
  *  struct and extern declarations only.  The actual definitions live in
  *  app_ui.c (core state + helpers) and in each page_*.c (its own widgets).
  ******************************************************************************
  */
#ifndef APP_UI_COMMON_H
#define APP_UI_COMMON_H

#include "lvgl.h"
#include "lv_font_gbk.h"
#include "bsp_lcd.h"
#include "bsp_lcd_text.h"
#include "bsp_touch.h"
#include "usb_host_app.h"
#include "sd_card.h"
#include "sensor_task.h"
#include "bsp_led.h"
#include "bsp_pcf8574.h"
#include "bsp_rtc.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "log.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Geometry -------------------------------------------------------------- */
#define UI_PAD      12
#define HDR_H       34
#define NAV_H       56
#define BAND_GAP    10
#define TOP_GAP     8

/* ---- Nav bar --------------------------------------------------------------- */
#define NAV_BTN_W   72
#define NAV_BTN_H   44

/* ---- Palette (minimal: dark bg, light text, no accents) -------------------- */
#define COL_BG      0x000000
#define COL_HDR     0x12161C
#define COL_TXT     0xD2D6DC
#define COL_DIM     0x808890
#define COL_BTN     0x1C222B

/* 2 Hz refresh: snappy enough for the sensor page, cheap enough for the pump. */
#define UI_REFRESH_MS  500U

/* ---- Pages ----------------------------------------------------------------- */
#define UI_PAGE_COUNT   4U

typedef enum
{
    PAGE_STATUS = 0,
    PAGE_HWINFO = 1,
    PAGE_CTRL   = 2,
    PAGE_RTC    = 3
} ui_page_t;

/* ---- Shared widget handles (one slot per page's widgets) ------------------- */
typedef struct
{
    /* page 0 - status */
    lv_obj_t *p0_sd;
    lv_obj_t *p0_usb;
    lv_obj_t *p0_font;
    lv_obj_t *p0_freq;
    lv_obj_t *p0_uptime;
    lv_obj_t *p0_cache;
    lv_obj_t *p0_f1;
    lv_obj_t *p0_f2;
    lv_obj_t *p0_f3;
    /* page 1 - hardware information */
    lv_obj_t *p1_ir;
    lv_obj_t *p1_als;
    lv_obj_t *p1_ps;
    lv_obj_t *p1_acc;
    lv_obj_t *p1_gyr;
    lv_obj_t *p1_mag;
    lv_obj_t *p1_stat;
    /* page 2 - device control */
    lv_obj_t *p2_led_btn;
    lv_obj_t *p2_beep_btn;
    lv_obj_t *p2_led_lbl;
    lv_obj_t *p2_beep_lbl;
    lv_obj_t *p2_led_state;
    lv_obj_t *p2_beep_state;
    /* page 3 - RTC clock (clock + edit widgets live in page_rtc.c) */
    lv_obj_t *page_lbl;
} ui_handles_t;

/* ---- Shared state (defined in app_ui.c, read by pages) --------------------- */
extern ui_handles_t s_ui;
extern int          s_page;
extern lv_coord_t   s_w;
extern lv_coord_t   s_h;
extern lv_coord_t   s_band_y[3];
extern lv_coord_t   s_band_h;
extern lv_coord_t   s_nav_y;
extern uint8_t      s_led_on;     /* LED1 (PB0) state shown on ctrl page */
extern uint8_t      s_beep_on;    /* buzzer (PCF8574 P0) state */
extern uint32_t     s_uptime_sec; /* system uptime seconds (core tick -> status page) */

/* ---- Shared widget helpers (defined in app_ui.c) --------------------------- */
lv_obj_t *mk_label(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                   const lv_font_t *font, uint32_t color, const char *text);
lv_obj_t *mk_label_center(lv_obj_t *parent, lv_coord_t y,
                          const lv_font_t *font, uint32_t color,
                          const char *text);
lv_obj_t *make_band(lv_obj_t *parent, lv_coord_t y, lv_coord_t h,
                    const char *title);
lv_obj_t *mk_nav_button(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                        const lv_point_t *chevron);
lv_obj_t *mk_ctrl_button(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                         lv_coord_t w, lv_coord_t h,
                         const char *title, int code);

/* ---- Page builders (one per file) ----------------------------------------- */
void build_page_status(void);
void build_page_hwinfo(void);
void build_page_ctrl(void);
void build_page_rtc(void);

/* ---- Per-page refreshers called by the core tick / page switch ------------- */
void refresh_usb(void);
void refresh_font(void);
void refresh_sd(void);
void refresh_runtime(void);
void refresh_ctrl(void);
void refresh_hwinfo(void);
void rtc_refresh_clock(void);   /* page_rtc: push RTC time into the clock labels */
void rtc_check_alarm(void);     /* page_rtc: software alarm compare (2 Hz tick) */

#ifdef __cplusplus
}
#endif

#endif /* APP_UI_COMMON_H */
