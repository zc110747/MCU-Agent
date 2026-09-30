/**
 * @file lv_port.h
 * @brief LVGL v9.1.0 port on top of the Phase 2 GLCDC framebuffer
 *
 * - display: LV_DISPLAY_RENDER_MODE_PARTIAL with a 800x40 line buffer,
 *   flush = memcpy into bsp_lcd_framebuffer()
 * - tick: RT-Thread millisecond tick (1 kHz kernel tick)
 * - thread: lv_timer_handler() loop at ~30 fps, created by lv_port_start()
 */
#ifndef LV_PORT_H_
#define LV_PORT_H_

#include <stdbool.h>
#include <stdint.h>

/** Create the LVGL thread and build the demo UI. Idempotent. */
void lv_port_start (void);

/** True once the LVGL thread is up. */
bool lv_port_running (void);

/** Total flush_cb invocations since start (each = one partial redraw). */
uint32_t lv_port_flush_count (void);

/** Frames fully pushed per second, sampled once a second. */
uint32_t lv_port_fps (void);

/** LVGL built-in allocator usage, in KB and percent of the pool. */
uint32_t lv_port_mem_used_kb (void);
uint32_t lv_port_mem_used_pct (void);

/** Size of the LVGL memory pool in KB (for the lv info printout). */
#define LV_PORT_MEM_TOTAL_KB  (128U)

/** Switch to a deterministic solid-rect test screen (for SWD pixel asserts).
 *  Safe to call from any thread: a flag is served inside the LVGL thread. */
void lv_port_test_screen (void);

/** Back to the animated demo screen. Thread-safe like lv_port_test_screen. */
void lv_port_demo_screen (void);

#endif /* LV_PORT_H_ */
