/**
 * @file lvgl_port.cpp
 * @brief LVGL <-> esp_lvgl_port binding for the 4.3" 800x480 RGB panel.
 *
 * WHY "direct mode" AND "avoid_tearing"
 * -------------------------------------
 * The panel is created with two frame buffers in PSRAM (BOARD_LCD_NUM_FB = 2).
 * With avoid_tearing the LVGL draw buffers *are* those two panel buffers - no
 * extra 800*480*2 byte buffer is allocated, and the flush path never copies
 * anything: esp_lcd_panel_draw_bitmap() recognises the pointer as one of its
 * own frame buffers and simply switches the scan-out to it (see
 * rgb_panel_draw_bitmap() -> draw_buf_copy_to_fb = false).  The frame switch
 * is deferred to VSYNC by the port, hence: no tearing, no memcpy.
 *
 * Direct mode (rather than full_refresh) is what makes this cheap for an e-book:
 * only the areas that actually changed are rendered, into both buffers
 * (lv_refr.c: refr_sync_areas() keeps the pair coherent).  A full refresh would
 * re-render all 768000 pixels on every single change.
 *
 * Two hard prerequisites, both already satisfied elsewhere - do not "clean
 * them up":
 *   1. BOARD_LCD_NUM_FB == 2. avoid_tearing reads exactly 2 buffers via
 *      esp_lcd_rgb_panel_get_frame_buffer(panel, 2, ...).
 *   2. CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y.  The frame buffer lives in PSRAM,
 *      so it is behind the data cache; the RGB driver derives
 *      flags.fb_behind_cache from the cache line size and only then performs
 *      the C2M cache sync that makes CPU writes visible to the DMA engine.
 *      Get this wrong and the panel shows stale pixels at random.
 */

#include "lvgl_port.h"

#include "board_config.h"
#include "display_driver.h"
#include "system_info.h"

#include "esp_lvgl_port.h"
#include "esp_log.h"

static const char *TAG = "lvgl_port";

static lv_display_t *s_display = NULL;
static bool s_started = false;

esp_err_t lvgl_port_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    /* ---- 1. the LVGL task / tick --------------------------------------
     * FREERTOS TASK CONTRACT (this is the whole UI's scheduling budget)
     * -------------------------------------------------------------------
     * esp_lvgl_port creates exactly one task, named "taskLVGL", which runs
     * lv_timer_handler() and therefore does *everything* UI: input device
     * polling, animation stepping, layout invalidation and rendering.
     *
     *   priority  6   above the application task (4) and everything the
     *                 driver layer starts, so a flush is never queued behind
     *                 background work; still far below the IPC (24) and
     *                 esp_timer (22) tasks the system needs to stay healthy.
     *   affinity  1   pinned to core 1 (APP_CPU).  Rendering competes for the
     *                 same PSRAM bandwidth the RGB DMA engine is streaming a
     *                 frame from, so keeping the UI on its own core stops it
     *                 from being time-sliced against app_main and the service
     *                 work on core 0 as well.
     *   stack     8192 rendering recurses through the widget tree.
     *   max_sleep 100  the task never naps longer than this; combined with
     *                 timer_period_ms below it bounds idle-to-flush latency.
     *   timer      5  200 Hz LVGL tick.  LVGL's own refresh period
     *                 (CONFIG_LV_DEF_REFR_PERIOD, 16 ms here) is what gates
     *                 how often a changed frame is actually flushed.
     */
    const lvgl_port_cfg_t port_cfg = {
        .task_priority = 6,
        .task_stack = 8192,          /* rendering happens on this stack */
        .task_affinity = 1,          /* APP_CPU: keep the UI off core 0 */
        .task_max_sleep_ms = 100,    /* idle sleep cap: snappier input */
        .task_stack_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT,
        .timer_period_ms = 5,        /* 200 Hz tick: touch + animations */
    };

    esp_err_t err = lvgl_port_init(&port_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lvgl_port_init: %s", esp_err_to_name(err));
        return err;
    }

    /* ---- 2. the panel as LVGL display 0 ------------------------------ */
    /* Assigned field by field rather than with designated initializers: C++
     * only allows designators for *direct* members, so the nested
     * `.rotation.swap_xy = ...` / `.flags.direct_mode = ...` form is a C
     * extension, not standard C++.  Plain assignments also survive any future
     * reordering of the struct.                                          */
    lvgl_port_display_cfg_t disp_cfg = {};
    disp_cfg.panel_handle   = display_panel();
    disp_cfg.buffer_size    = BOARD_LCD_H_RES * BOARD_LCD_V_RES; /* forced by avoid_tearing */
    disp_cfg.double_buffer  = true;
    disp_cfg.hres           = BOARD_LCD_H_RES;
    disp_cfg.vres           = BOARD_LCD_V_RES;
    disp_cfg.monochrome     = false;                 /* RGB565, not 1bpp      */
    disp_cfg.color_format   = LV_COLOR_FORMAT_RGB565;
    disp_cfg.rotation.swap_xy  = false;              /* landscape is native   */
    disp_cfg.rotation.mirror_x = false;
    disp_cfg.rotation.mirror_y = false;
    disp_cfg.flags.swap_bytes   = false;             /* see the note below    */
    disp_cfg.flags.full_refresh = false;
    disp_cfg.flags.direct_mode  = true;

    lvgl_port_display_rgb_cfg_t rgb_cfg = {};
    /* bb_mode must agree with display_driver.c's bounce_buffer_size_px: it
     * only selects *which* RGB panel event releases the LVGL draw buffer.
     * With a bounce buffer the frame is finished when the driver has copied
     * the whole frame buffer into the bounce buffers (on_frame_buf_complete);
     * without one it is finished at VSYNC.  Getting the pair out of step
     * leaves LVGL waiting on an event that never arrives. */
    rgb_cfg.flags.bb_mode       = true;              /* bounce buffer mode   */
    rgb_cfg.flags.avoid_tearing = true;

    lvgl_port_acquire(0);
    s_display = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    lvgl_port_release();

    if (s_display == NULL) {
        ESP_LOGE(TAG, "lvgl_port_add_disp_rgb failed");
        return ESP_FAIL;
    }

    lv_display_set_default(s_display);

    s_started = true;
    ESP_LOGI(TAG, "LVGL %d.%d.%d  %dx%d RGB565  direct mode, 2 panel FBs, no extra draw buffer",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
             BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    return ESP_OK;
}

bool lvgl_port_acquire(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void lvgl_port_release(void)
{
    lvgl_port_unlock();
}

lv_display_t *lvgl_port_display(void)
{
    return s_display;
}
