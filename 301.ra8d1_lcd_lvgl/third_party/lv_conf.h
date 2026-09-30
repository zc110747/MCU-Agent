/**
 * @file lv_conf.h
 * @brief LVGL v9.1.0 config - overrides only (everything else keeps the
 *        defaults from lvgl/src/lv_conf_internal.h).
 *
 * Vendor rule: the LVGL sources under third_party/lvgl/ are upstream as-is;
 * this file is the only project-specific knob.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

/* ------------------------------------------------------------- color format
 * Framebuffer pixels are plain little-endian RGB565 uint16 (same format the
 * Phase 2 bsp_lcd_fill()/pattern path writes and the GLCDC reads), so no
 * byte swap. */
#define LV_COLOR_DEPTH              16
#define LV_COLOR_16_SWAP            0

/* -------------------------------------------------------------------- memory
 * Built-in allocator with a static pool in internal SRAM (plenty of room:
 * self-code .bss is ~67 KB of the 1 MB bank). */
#define LV_USE_STDLIB_MALLOC        LV_STDLIB_BUILTIN
#define LV_MEM_SIZE                 (128U * 1024U)

/* --------------------------------------------------------------- OS and tick
 * Bare RT-Thread Nano: no LVGL OSAL. NOTE: v9 dropped the v8-style
 * LV_TICK_CUSTOM macro - the tick source is registered at runtime with
 * lv_tick_set_cb() in lv_port.c (rt_tick_get_millisecond, 1 kHz kernel tick). */
#define LV_USE_OS                   LV_OS_NONE

/* ------------------------------------------------------------------ drawing
 * One SW render unit, 33 ms refresh period (~30 fps target). */
#define LV_USE_LOG                  0
#define LV_DRAW_SW_DRAW_UNIT_CNT    1
#define LV_DEF_REFR_PERIOD          33

/* ------------------------------------------------------------------- asserts
 * Off: acceptance is driven by verify scripts, not by LVGL's own aborts. */
#define LV_USE_ASSERT_NULL          0
#define LV_USE_ASSERT_MALLOC        0
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0
#define LV_USE_PERF_MONITOR         0
#define LV_USE_MEM_MONITOR          0

/* -------------------------------------------------------------------- themes
 * Default theme only (dark), Montserrat 14 as the base font. */
#define LV_USE_THEME_DEFAULT        1
#define LV_FONT_MONTSERRAT_14       1

#endif /* LV_CONF_H */
