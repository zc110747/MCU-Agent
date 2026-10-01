/**
 ******************************************************************************
 * @file    app_ui.c
 * @brief   UI orchestrator - see app_ui.h.
 *
 *  Structure mirrors the reference project 003.stm32h743_lvgl_oled/app/app_ui.c:
 *  this module owns the page array, the boot/loading gate and page switching.
 *  The pages themselves live in their own files (ui_page_*.c).
 *
 *  Differences for this board: the display is 480x360 and there is no font
 *  preload queue, so the boot bar simply tracks the mandatory 2 s dwell.
 *
 *  Threading: this file is only ever entered from the LVGL thread (via the
 *  lv_timer callbacks below).  The msh command posts requests through the
 *  volatile g_req_* flags, which app_ui_service() drains.
 ******************************************************************************
 */
#include "app_ui.h"
#include "ui_common.h"
#include "ui_page_boot.h"
#include "ui_page_menu.h"
#include "ui_page_info.h"
#include "lv_port.h"

#include <rtthread.h>

/* Boot gate: minimum on-screen dwell before the menu appears. */
#define BOOT_MIN_MS 2000U

/* Menu action plumbing: actions are served here so they run on the LVGL
   thread (they call LVGL APIs and re-enter the page system). */
typedef enum
{
    REQ_NONE = 0,
    REQ_UP,
    REQ_DOWN,
    REQ_ENTER,
    REQ_SELECT,
    REQ_BACK
} ui_req_t;

static lv_obj_t *s_pages[APP_UI_PAGE_COUNT];
static uint8_t   s_cur_page = APP_UI_BOOT;

/* Boot gate. */
static uint32_t s_boot_t0;

/* Cross-thread request mailbox. */
static volatile ui_req_t s_req        = REQ_NONE;
static volatile uint8_t  s_req_index  = 0U;
static volatile uint8_t  s_last_action = 0U;

/* -------------------------------------------------------------------------- */
/* Page switching                                                             */
/* -------------------------------------------------------------------------- */

void app_ui_show(uint8_t page_id)
{
    if (page_id >= APP_UI_PAGE_COUNT)
    {
        return;
    }
    if (s_pages[page_id] == NULL)
    {
        return;
    }

    s_cur_page = page_id;
    lv_screen_load(s_pages[page_id]);
}

uint8_t app_ui_current(void)
{
    return s_cur_page;
}

/* -------------------------------------------------------------------------- */
/* Boot gate                                                                  */
/* -------------------------------------------------------------------------- */

static void boot_timer_cb(lv_timer_t *timer)
{
    uint32_t elapsed  = (uint32_t) rt_tick_get_millisecond() - s_boot_t0;
    uint32_t time_pct = (elapsed * 100U) / BOOT_MIN_MS;
    uint32_t bar;

    if (time_pct > 100U)
    {
        time_pct = 100U;
    }

    bar = time_pct;                 /* no preload queue on this board */
    ui_page_boot_set((uint8_t) bar);

    if (time_pct >= 100U)
    {
        rt_kprintf("[boot] UI ready (%u ms)\n", (unsigned int) elapsed);
        app_ui_show(APP_UI_MENU);
        lv_timer_del(timer);
    }
}

/* -------------------------------------------------------------------------- */
/* Request service (LVGL thread)                                              */
/* -------------------------------------------------------------------------- */

static void run_action(uint8_t action)
{
    switch (action)
    {
    case APP_UI_ACT_TEST:
        lv_port_test_screen();
        break;
    case APP_UI_ACT_FILL:
        lv_port_demo_screen();
        break;
    case APP_UI_ACT_DEMO:
        lv_port_demo_screen();
        break;
    case APP_UI_ACT_INFO:
        app_ui_show(APP_UI_INFO);
        break;
    default:
        break;
    }
    s_last_action = action;
}

void app_ui_service(void)
{
    ui_req_t req = s_req;

    if (req == REQ_NONE)
    {
        return;
    }
    s_req = REQ_NONE;               /* claim before acting */

    switch (req)
    {
    case REQ_UP:
        ui_page_menu_move(-1);
        break;
    case REQ_DOWN:
        ui_page_menu_move(+1);
        break;
    case REQ_SELECT:
        ui_page_menu_select(s_req_index);
        break;
    case REQ_ENTER:
        run_action(ui_page_menu_enter());
        break;
    case REQ_BACK:
        app_ui_show(APP_UI_MENU);
        break;
    default:
        break;
    }
}

/* Request posts (any thread). */
void app_ui_req_up(void)     { s_req = REQ_UP; }
void app_ui_req_down(void)   { s_req = REQ_DOWN; }
void app_ui_req_enter(void)  { s_req = REQ_ENTER; }
void app_ui_req_back(void)   { s_req = REQ_BACK; }

void app_ui_req_select(uint8_t index)
{
    s_req_index = index;
    s_req       = REQ_SELECT;
}

/* Read-back (any thread). */
uint8_t     app_ui_menu_index(void)         { return ui_page_menu_index(); }
uint8_t     app_ui_menu_count(void)         { return ui_page_menu_count(); }
const char *app_ui_menu_label(uint8_t index) { return ui_page_menu_label(index); }
uint8_t     app_ui_last_action(void)        { return s_last_action; }

/* -------------------------------------------------------------------------- */
/* Build                                                                      */
/* -------------------------------------------------------------------------- */

void app_ui_create(void)
{
    /* Build every page off-screen first: the boot page is shown while the
       rest are already allocated, so the first switch is allocation-free. */
    s_pages[APP_UI_BOOT] = ui_page_boot_build();
    s_pages[APP_UI_MENU] = ui_page_menu_build();
    s_pages[APP_UI_INFO] = ui_page_info_build();

    lv_screen_load(s_pages[APP_UI_BOOT]);
    ui_page_boot_set_status("System starting...");

    s_cur_page = APP_UI_BOOT;
    s_boot_t0  = (uint32_t) rt_tick_get_millisecond();
    (void) lv_timer_create(boot_timer_cb, 50, NULL);
}
