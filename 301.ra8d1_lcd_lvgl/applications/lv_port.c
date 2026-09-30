/**
 * @file lv_port.c
 * @brief LVGL v9.1.0 port: partial renderer + memcpy flush + RT-Thread tick
 *
 * Threading model: only the LVGL thread touches LVGL objects. The msh
 * commands only flip volatile flags (screen switch) or read 32-bit counters
 * (fps/flushes/mem), both atomic on Cortex-M85.
 */
#include "lv_port.h"

#include <rtthread.h>
#include <lvgl.h>

#include "bsp_lcd.h"

/* ---- render buffer: 40 full lines of RGB565 in internal SRAM ------------- */
#define DRAW_BUF_LINES   (40U)
#define DRAW_BUF_BYTES   (BSP_LCD_WIDTH * DRAW_BUF_LINES * (BSP_LCD_BPP / 8U))

static uint8_t g_draw_buf[DRAW_BUF_BYTES] __attribute__((aligned(32)));

/* ---- state --------------------------------------------------------------- */
static rt_thread_t       g_lvgl_thread;
static lv_display_t *    g_disp;
static volatile bool     g_running;
static volatile bool     g_test_req;
static volatile bool     g_demo_req;
static volatile uint32_t g_flush_count;
static volatile uint32_t g_fps;

static lv_obj_t * g_lbl_uptime;

/* ---- flush: partial buffer -> SDRAM framebuffer -------------------------- */
static void flush_cb (lv_display_t * disp, const lv_area_t * area,
                      uint8_t * px_map)
{
    uint16_t *      fb  = bsp_lcd_framebuffer();
    int32_t         w   = area->x2 - area->x1 + 1;
    int32_t         h   = area->y2 - area->y1 + 1;
    const uint8_t * src = px_map;

    for (int32_t row = 0; row < h; row++)
    {
        uint32_t dst_off = (uint32_t) ((area->y1 + row) * BSP_LCD_WIDTH +
                                       area->x1);
        (void) rt_memcpy(&fb[dst_off], src, (size_t) w * 2U);
        src += (size_t) w * 2U;
    }

    lv_display_flush_ready(disp);
    g_flush_count++;
}

/* ---- anim exec wrappers (LVGL calls these as void(*)(void*, int32_t)) ----- */
static void bar_anim_cb (void * var, int32_t v)
{
    lv_bar_set_value((lv_obj_t *) var, v, LV_ANIM_OFF);
}

static void arc_anim_cb (void * var, int32_t v)
{
    lv_arc_set_value((lv_obj_t *) var, v);
}

/* ---- tick source: v9 has no LV_TICK_CUSTOM, register a callback instead --- */
static uint32_t lv_port_tick_ms (void)
{
    return (uint32_t) rt_tick_get_millisecond();
}

/* ---- one-second housekeeping: uptime, fps -------------------------------- */
static void sec_timer_cb (lv_timer_t * timer)
{
    static uint32_t last_flush;
    static uint32_t start_sec;

    uint32_t now = (uint32_t) rt_tick_get_millisecond() / 1000U;
    uint32_t up  = now - start_sec;
    uint32_t fc  = g_flush_count;

    g_fps = fc - last_flush;
    last_flush = fc;

    if (g_lbl_uptime != NULL)
    {
        lv_label_set_text_fmt(g_lbl_uptime, "uptime %u.%02u s   fps %u",
                              (unsigned int) up,
                              (unsigned int) (up * 100U % 100U),
                              (unsigned int) g_fps);
    }

    LV_UNUSED(timer);
}

/* ---- demo screen (minimal dark, ASCII text: Montserrat has no CJK) ------- */
static void build_demo_screen (void)
{
    lv_obj_t * scr = lv_screen_active();

    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);

    lv_obj_t * title = lv_label_create(scr);
    lv_label_set_text(title, "RA8D1 Vision Board - LVGL 9.1.0");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE8EAED), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t * sub = lv_label_create(scr);
    lv_label_set_text(sub, "Cortex-M85 480MHz - GLCDC RGB565 800x480");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x9AA0A6), 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 38);

    g_lbl_uptime = lv_label_create(scr);
    lv_obj_set_style_text_color(g_lbl_uptime, lv_color_hex(0xE8EAED), 0);
    lv_obj_align(g_lbl_uptime, LV_ALIGN_TOP_MID, 0, 64);
    lv_label_set_text(g_lbl_uptime, "uptime 0.00 s   fps 0");

    /* animated bar: 0 -> 100 -> 0 loop */
    lv_obj_t * bar = lv_bar_create(scr);
    lv_obj_set_size(bar, 400, 16);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 100);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, bar);
    lv_anim_set_exec_cb(&a, bar_anim_cb);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_duration(&a, 2000);
    lv_anim_set_playback_duration(&a, 2000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);

    /* spinning arc */
    lv_obj_t * arc = lv_arc_create(scr);
    lv_obj_set_size(arc, 120, 120);
    lv_obj_align(arc, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_anim_t b;
    lv_anim_init(&b);
    lv_anim_set_var(&b, arc);
    lv_anim_set_exec_cb(&b, arc_anim_cb);
    lv_anim_set_values(&b, 0, 359);
    lv_anim_set_duration(&b, 3000);
    lv_anim_set_repeat_count(&b, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&b);

    /* RGBW chips: prove all four 565 channel extremes reach the panel.
       NOTE: lv_color_hex() takes RGB888 (0xRRGGBB) - LVGL converts to the
       565 framebuffer format itself. Passing 565 values here renders the
       wrong hue (e.g. 0xF800 becomes bright green). */
    static const uint32_t chip_col[4] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF };

    for (uint32_t i = 0; i < 4U; i++)
    {
        lv_obj_t * chip = lv_obj_create(scr);
        lv_obj_set_size(chip, 60, 24);
        lv_obj_align(chip, LV_ALIGN_TOP_LEFT, (int32_t) (16 + i * 80), 400);
        lv_obj_set_style_bg_color(chip, lv_color_hex(chip_col[i]), 0);
        lv_obj_set_style_radius(chip, 0, 0);
        lv_obj_set_style_border_width(chip, 0, 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    }
}

/* ---- deterministic test screen for SWD pixel asserts ----------------------
   Solid rects, no radius/border/shadow. Center pixels are pure colors. */
static void build_test_screen (void)
{
    static const struct
    {
        uint32_t x, y, w, h, col;      /* col is RGB888 for lv_color_hex() */
    } rects[] =
    {
        { 10,  10, 100, 60, 0xFF0000 },   /* red   -> fb 0xF800 */
        {120,  10, 100, 60, 0x00FF00 },   /* green -> fb 0x07E0 */
        {230,  10, 100, 60, 0x0000FF },   /* blue  -> fb 0x001F */
        {340,  10, 100, 60, 0xFFFFFF },   /* white -> fb 0xFFFF */
    };

    lv_obj_t * scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

    for (uint32_t i = 0; i < 4U; i++)
    {
        lv_obj_t * r = lv_obj_create(scr);
        lv_obj_set_pos(r, (int32_t) rects[i].x, (int32_t) rects[i].y);
        lv_obj_set_size(r, (int32_t) rects[i].w, (int32_t) rects[i].h);
        lv_obj_set_style_bg_color(r, lv_color_hex(rects[i].col), 0);
        lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(r, 0, 0);
        lv_obj_set_style_border_width(r, 0, 0);
        lv_obj_set_style_pad_all(r, 0, 0);
        lv_obj_set_scrollbar_mode(r, LV_SCROLLBAR_MODE_OFF);
    }

    /* labels live on the demo screen only */
    g_lbl_uptime = NULL;
}

/* ---- screen-mode requests, served inside the LVGL thread ----------------- */
static void mode_timer_cb (lv_timer_t * timer)
{
    if (g_test_req)
    {
        g_test_req = false;
        lv_obj_clean(lv_screen_active());
        build_test_screen();
    }
    else if (g_demo_req)
    {
        g_demo_req = false;
        lv_obj_clean(lv_screen_active());
        build_demo_screen();
    }

    LV_UNUSED(timer);
}

/* ---- LVGL thread ---------------------------------------------------------- */
static void lvgl_thread_entry (void * param)
{
    lv_init();
    lv_tick_set_cb(lv_port_tick_ms);

    g_disp = lv_display_create(BSP_LCD_WIDTH, BSP_LCD_HEIGHT);
    lv_display_set_flush_cb(g_disp, flush_cb);
    lv_display_set_buffers(g_disp, g_draw_buf, NULL, DRAW_BUF_BYTES,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    /* dark theme matches the project's minimal dark UI style */
    (void) lv_theme_default_init(g_disp,
                                 lv_color_hex(0x4C8DFF), lv_color_hex(0x3DDC84),
                                 true, &lv_font_montserrat_14);

    build_demo_screen();

    lv_timer_create(sec_timer_cb, 1000, NULL);
    lv_timer_create(mode_timer_cb, 50, NULL);

    g_running = true;

    while (1)
    {
        uint32_t next = lv_timer_handler();

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

void lv_port_test_screen (void)
{
    if (g_running)
    {
        g_test_req = true;
    }
}

void lv_port_demo_screen (void)
{
    if (g_running)
    {
        g_demo_req = true;
    }
}
