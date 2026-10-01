/**
 * @file lv_port.c
 * @brief LVGL v9.1.0 port: DIRECT renderer, page swap via R_GLCDC_BufferChange
 *
 * Phase 5 changed the display path from "partial buffer memcpy'd into one
 * framebuffer" to LVGL's DIRECT mode: LVGL renders straight into one of the
 * two SDRAM pages that bsp_lcd owns, then the flush callback hands that page
 * to the GLCDC with R_GLCDC_BufferChange() and waits for the frame boundary
 * (GLCDC line-detect interrupt) before returning, so the panel never samples
 * a page while it is still being written.
 *
 * Threading model: only the LVGL thread touches LVGL objects. The msh
 * commands only flip volatile flags (screen switch) or read 32-bit counters
 * (fps/flushes/mem), both atomic on Cortex-M85.
 *
 * Phase 7 removed the touch input device: the 2.0" panel fitted to this board
 * does not answer any I2C transaction (see documents/phase5-touch-report.md),
 * so the screen is display-only and the UI is driven from the debug console.
 *
 * Phase 8 adds a boot splash with an animated action bar: the LVGL thread
 * builds ui_page_boot first, then mode_timer_cb swaps to the main screen once
 * the bar has filled (see applications/ui/ui_page_boot.c).
 */
#include "lv_port.h"

#include <rtthread.h>
#include <lvgl.h>

#include "bsp_lcd.h"
#include "ui_page_main.h"
#include "ui_page_boot.h"

/* ---- state --------------------------------------------------------------- */
static rt_thread_t       g_lvgl_thread;
static lv_display_t *    g_disp;
static rt_sem_t          g_vsync_sem;
static volatile bool     g_running;
static volatile bool     g_test_req;
static volatile bool     g_main_req;
static volatile uint32_t g_flush_count;
static volatile uint32_t g_fps;
static volatile uint32_t g_loop_count;
static volatile uint32_t g_handler_count;

/* ---- frame-boundary gate -------------------------------------------------
   Signalled from the GLCDC line-detect ISR (bsp_lcd.c).  Only waited on once
   the LVGL driver is up, mirroring the official port. */
static void vsync_wait_cb (lv_display_t * display)
{
    if (!lv_display_flush_is_last(display))
    {
        return;
    }

    if (g_vsync_sem != NULL)
    {
        (void) rt_sem_take(g_vsync_sem, RT_WAITING_FOREVER);
    }
}

/* ---- flush: publish the freshly rendered page ---------------------------- */
static void flush_cb (lv_display_t * disp, const lv_area_t * area,
                      uint8_t * px_map)
{
    LV_UNUSED(area);

    /* The wait callback only blocks on the last flush of a frame, so a
       non-last call is a no-op here: DIRECT mode renders the whole screen
       into one buffer and the GLCDC reads it from SDRAM. */
    if (!lv_display_flush_is_last(disp))
    {
        return;
    }

    /* BSP_CFG_DCACHE_ENABLED is 0 in this project, so this is a no-op today;
       it is kept because it becomes load-bearing the moment the D-cache is
       turned on (the GLCDC is a second bus master on the same SDRAM). */
    SCB_CleanInvalidateDCache_by_Addr((void *) px_map, (int32_t) BSP_LCD_FB_BYTES);

    (void) bsp_lcd_set_framebuffer(px_map);

    lv_display_flush_ready(disp);
    g_flush_count++;
}

/* ---- tick source: v9 has no LV_TICK_CUSTOM, register a callback instead --- */
static uint32_t lv_port_tick_ms (void)
{
    return (uint32_t) rt_tick_get_millisecond();
}

/* ---- one-second housekeeping: fps counter -------------------------------- */
static void sec_timer_cb (lv_timer_t * timer)
{
    static uint32_t last_flush;

    uint32_t fc = g_flush_count;

    g_fps = fc - last_flush;
    last_flush = fc;

    LV_UNUSED(timer);
}

/* ---- the one screen ------------------------------------------------------
   Phase 7 replaced the boot/menu/info page system with a single screen:
   board identity, live hardware telemetry and the hardware-RTC clock.  The
   content lives in applications/ui/ui_page_main.c; lv_port owns only the
   display driver, the tick, the flush path and the thread. */
static void build_main_screen (void)
{
    lv_obj_t * scr = ui_page_main_build();

    lv_screen_load(scr);
}

/* ---- boot splash + action bar --------------------------------------------
   The panel used to show a frozen 8-colour-bar pattern between bsp_lcd_init()
   and the first LVGL frame.  It now shows a real boot screen with an animated
   action bar: this builds it, and mode_timer_cb hands over to the main screen
   once the bar has filled. */
static void build_boot_screen (void)
{
    lv_obj_t * scr = ui_page_boot_build();

    lv_screen_load(scr);
}

/* ---- deterministic test screen for SWD pixel asserts ----------------------
   Solid rects, no radius/border/shadow. Center pixels are pure colors.
   Row 10..70 / cols spread across 480: four 100 px rects starting at
   10 / 130 / 250 / 370.  Built once and kept as a hidden page so the menu
   page is never destroyed (returning to it is then allocation-free). */
static lv_obj_t * g_test_scr;

static void build_test_screen (void)
{
    static const struct
    {
        uint32_t x, y, w, h, col;      /* col is RGB888 for lv_color_hex() */
    } rects[] =
    {
        {  10, 10, 100, 60, 0xFF0000 },   /* red   -> fb 0xF800 */
        { 130, 10, 100, 60, 0x00FF00 },   /* green -> fb 0x07E0 */
        { 250, 10, 100, 60, 0x0000FF },   /* blue  -> fb 0x001F */
        { 370, 10, 100, 60, 0xFFFFFF },   /* white -> fb 0xFFFF */
    };

    if (g_test_scr == NULL)
    {
        g_test_scr = lv_obj_create(NULL);
        lv_obj_clear_flag(g_test_scr, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(g_test_scr, 0, 0);
    }

    lv_obj_clean(g_test_scr);
    lv_obj_set_style_bg_color(g_test_scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_test_scr, LV_OPA_COVER, 0);

    for (uint32_t i = 0; i < 4U; i++)
    {
        lv_obj_t * r = lv_obj_create(g_test_scr);
        lv_obj_set_pos(r, (int32_t) rects[i].x, (int32_t) rects[i].y);
        lv_obj_set_size(r, (int32_t) rects[i].w, (int32_t) rects[i].h);
        lv_obj_set_style_bg_color(r, lv_color_hex(rects[i].col), 0);
        lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(r, 0, 0);
        lv_obj_set_style_border_width(r, 0, 0);
        lv_obj_set_style_pad_all(r, 0, 0);
        lv_obj_set_scrollbar_mode(r, LV_SCROLLBAR_MODE_OFF);
    }

    lv_screen_load(g_test_scr);
}

/* ---- screen-mode requests, served inside the LVGL thread ----------------- */
static volatile bool g_boot_active;    /* boot splash owns the panel       */

static void mode_timer_cb (lv_timer_t * timer)
{
    /* Hand over from the boot splash as soon as its action bar fills, unless
       the user explicitly asked for another screen meanwhile. */
    if (g_boot_active && ui_page_boot_done())
    {
        g_boot_active = false;
        build_main_screen();
    }

    if (g_test_req)
    {
        g_test_req = false;
        g_boot_active = false;
        build_test_screen();
    }
    else if (g_main_req)
    {
        g_main_req = false;
        g_boot_active = false;
        build_main_screen();
    }

    LV_UNUSED(timer);
}

/* ---- LVGL thread ---------------------------------------------------------- */
static void lvgl_thread_entry (void * param)
{
    lv_init();
    lv_tick_set_cb(lv_port_tick_ms);

    /* Initial value 1, matching the official MIPI reference port: the first
       frame must not block waiting for a boundary that may already have
       passed (GLCDC is started in bsp_lcd_init(), before this thread exists). */
    g_vsync_sem = rt_sem_create("lvvsync", 1, RT_IPC_FLAG_PRIO);
    if (g_vsync_sem == NULL)
    {
        rt_kprintf("lv start: vsync sem FAILED\n");
    }

    g_disp = lv_display_create(BSP_LCD_WIDTH, BSP_LCD_HEIGHT);
    lv_display_set_flush_cb(g_disp, flush_cb);
    lv_display_set_flush_wait_cb(g_disp, vsync_wait_cb);

    /* DIRECT mode: two full-screen pages owned by bsp_lcd.  LVGL renders into
       one while the GLCDC scans the other; the flush swaps them. */
    lv_display_set_buffers(g_disp,
                           bsp_lcd_framebuffer_page(0),
                           bsp_lcd_framebuffer_page(1),
                           (uint32_t) BSP_LCD_FB_BYTES,
                           LV_DISPLAY_RENDER_MODE_DIRECT);

    /* dark theme matches the project's minimal dark UI style */
    (void) lv_theme_default_init(g_disp,
                                 lv_color_hex(0x4C8DFF), lv_color_hex(0x3DDC84),
                                 true, &lv_font_montserrat_14);

    /* Phase 8: show the boot splash first; mode_timer_cb swaps to the main
       screen once its action bar reaches 100%. */
    g_boot_active = true;
    build_boot_screen();

    lv_timer_create(sec_timer_cb, 1000, NULL);
    lv_timer_create(mode_timer_cb, 50, NULL);

    g_running = true;

    while (1)
    {
        g_loop_count++;

        uint32_t next = lv_timer_handler();

        g_handler_count++;

        if (next > LV_DEF_REFR_PERIOD)
        {
            next = LV_DEF_REFR_PERIOD;
        }

        rt_thread_mdelay(next);
    }

    LV_UNUSED(param);
}

/* ---- public API ----------------------------------------------------------- */
void lv_port_start (void)
{
    if (g_lvgl_thread != NULL)
    {
        return;
    }

    g_lvgl_thread = rt_thread_create("lvgl", lvgl_thread_entry, NULL,
                                     8192U, 20U, 10U);
    if (g_lvgl_thread == NULL)
    {
        rt_kprintf("lv start: thread create FAILED\n");
        return;
    }

    rt_thread_startup(g_lvgl_thread);
}

bool lv_port_running (void)
{
    return g_running;
}

uint32_t lv_port_flush_count (void)
{
    return g_flush_count;
}

uint32_t lv_port_fps (void)
{
    return g_fps;
}

uint32_t lv_port_mem_used_kb (void)
{
    lv_mem_monitor_t mon;

    lv_mem_monitor(&mon);
    return (uint32_t) ((mon.total_size - mon.free_size) / 1024U);
}

uint32_t lv_port_mem_used_pct (void)
{
    lv_mem_monitor_t mon;

    lv_mem_monitor(&mon);
    return (uint32_t) mon.used_pct;
}

uint32_t lv_port_loop_count (void)
{
    return g_loop_count;
}

uint32_t lv_port_handler_count (void)
{
    return g_handler_count;
}

void lv_port_test_screen (void)
{
    if (g_running)
    {
        g_test_req = true;
    }
}

void lv_port_main_screen (void)
{
    if (g_running)
    {
        g_main_req = true;
    }
}

/* ---- vsync plumbing ------------------------------------------------------ */
void lv_port_vsync_notify (void)
{
    if (g_vsync_sem != NULL)
    {
        (void) rt_sem_release(g_vsync_sem);
    }
}
