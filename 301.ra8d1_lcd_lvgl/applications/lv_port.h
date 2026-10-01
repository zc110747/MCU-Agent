/**
 * @file lv_port.h
 * @brief LVGL v9.1.0 port on top of the MIPI DSI panel via the GLCDC
 *
 * - display: DIRECT mode, two full-screen SDRAM pages, flush = R_GLCDC_BufferChange
 * - input:   CST812T touch panel as an LV_INDEV_TYPE_POINTER indev (polled)
 * - tick:    RT-Thread millisecond tick (1 kHz kernel tick)
 * - thread:  lv_timer_handler() loop at ~30 fps, created by lv_port_start()
 */
#ifndef LV_PORT_H_
#define LV_PORT_H_

#include <stdbool.h>
#include <stdint.h>

/** Create the LVGL thread and build the demo UI. Idempotent. */
void lv_port_start (void);

/** True once the LVGL thread is up. */
bool lv_port_running (void);

/** Total flush_cb invocations since start (each = one page publish). */
uint32_t lv_port_flush_count (void);

/** Frames fully pushed per second, sampled once a second. */
uint32_t lv_port_fps (void);

/** LVGL built-in allocator usage, in KB and percent of the pool. */
uint32_t lv_port_mem_used_kb (void);
uint32_t lv_port_mem_used_pct (void);

/** Loop / lv_timer_handler() iteration counters (diagnostics: if loop_count
 *  advances but flush_count is frozen, the thread is inside the handler). */
uint32_t lv_port_loop_count (void);
uint32_t lv_port_handler_count (void);

/** True when the CST812T touch indev was registered (chip answered at init). */
bool lv_port_touch_active (void);

/** Size of the LVGL memory pool in KB (for the lv info printout). */
#define LV_PORT_MEM_TOTAL_KB  (128U)

/** Called from the GLCDC line-detect ISR (via bsp_lcd_vsync_notify) to
 *  release the frame-boundary semaphore the flush waits on.  Safe to call
 *  before lv_port_start(): it becomes a no-op until the semaphore exists. */
void lv_port_vsync_notify (void);

/** Switch to a deterministic solid-rect test screen (for SWD pixel asserts).
 *  Safe to call from any thread: a flag is served inside the LVGL thread. */
void lv_port_test_screen (void);

/** Back to the animated demo screen. Thread-safe like lv_port_test_screen. */
void lv_port_demo_screen (void);

#endif /* LV_PORT_H_ */
