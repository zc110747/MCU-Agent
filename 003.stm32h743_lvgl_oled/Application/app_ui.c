/**
  ******************************************************************************
  * @file    app_ui.c
  * @brief   UI orchestrator: page array, boot/loading gate, page rotation.
  *
  *  Pages themselves live in their own files (ui_page_*.c).  This module only
  *  owns the page array, the 5 s auto-rotation between the two content pages,
  *  and the boot sequence:
  *
  *    1. Build the info + font pages off-screen so their labels enqueue the
  *       TTF glyph preload.
  *    2. Build and show the boot/loading page.
  *    3. A 50 ms timer fills the progress bar.  It is gated on BOTH a minimum
  *       2 s dwell and (for the TTF engine) the preload queue draining, then it
  *       loads page 0, pins its glyphs (epoch bump) and starts the rotation.
  *       For the GBK engine nothing is preloaded, so the bar simply animates
  *       across the mandatory 2 s.
  ******************************************************************************
  */
#include "app_ui.h"
#include "log.h"
#include "ui_common.h"
#include "ui_page_info.h"
#include "ui_page_font.h"
#include "ui_page_boot.h"
#include "lv_font_provider.h"
#include "lvgl.h"
#include "lvgl_font.h"
#include "main.h"

/* Page auto-rotation (5 s) + boot gate. */
#define PAGE_SWITCH_MS    5000U
#define PAGE_COUNT        2U
#define BOOT_MIN_MS       2000U

static lv_obj_t   *s_pages[PAGE_COUNT];
static uint8_t     s_cur_page       = 0U;
static uint8_t     s_switch_pending = 0U;

/* Boot / preload gating. */
static uint32_t    s_boot_t0;        /* HAL_GetTick() at boot-page show   */
static uint32_t    s_boot_pending0;  /* preload queue depth captured at build */

/*----------------------------------------------------------------------------*/
/* Small helpers                                                              */
/*----------------------------------------------------------------------------*/

static void dwt_ensure_enabled(void)
{
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0u)
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0u;
        DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
    }
}

static void page_switch_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    s_cur_page ^= 1U;
    s_switch_pending = 1U;
}

/* -------------------------------------------------------------------------- */
/* Boot warm-up                                                               */
/* -------------------------------------------------------------------------- */

/* A screen's first ever LVGL render does ~190 ms of one-time work (style
 * value computation, label layout, draw-task build) on top of the steady-state
 * repaint.  That is exactly the "first load is ~200 ms slower than the second"
 * the user reported.  Rendering every content page once at boot - with the
 * display flush suppressed so nothing reaches the panel - moves that cost to
 * the boot screen and leaves each real first switch warm. */
static void dummy_flush_cb(lv_disp_drv_t *drv,
                           const lv_area_t *area, lv_color_t *color_p)
{
    LV_UNUSED(area);
    LV_UNUSED(color_p);
    lv_disp_flush_ready(drv);
}

static void ui_warmup_pages(void)
{
    lv_disp_t        *disp = lv_disp_get_default();
    lv_disp_drv_t    *drv  = (disp != NULL) ? disp->driver : NULL;
    void (*orig)(lv_disp_drv_t *, const lv_area_t *, lv_color_t *) = NULL;
    uint8_t           i;

    if (drv == NULL)
    {
        return;
    }

    orig       = drv->flush_cb;
    drv->flush_cb = dummy_flush_cb;     /* swallow the framebuffer push */

    for (i = 0u; i < PAGE_COUNT; i++)
    {
        lv_scr_load(s_pages[i]);
        (void)lv_timer_handler();        /* first paint, hidden */
    }

    drv->flush_cb = orig;
}

uint8_t app_ui_take_switch(lv_obj_t **out_screen, int *out_index)
{
    if ((s_switch_pending != 0U) && (out_screen != NULL))
    {
        *out_screen = s_pages[s_cur_page];
        if (out_index != NULL)
        {
            *out_index = (int)s_cur_page;
        }
        s_switch_pending = 0U;
        /* The new page is now on screen: pin its glyphs and let the old page's
         * fall out of the LRU cache under pressure. */
        lv_font_provider_on_page_shown();
        return 1U;
    }
    return 0U;
}

/*----------------------------------------------------------------------------*/
/* Boot gate                                                                  */
/*----------------------------------------------------------------------------*/

static void boot_timer_cb(lv_timer_t *timer)
{
    uint32_t elapsed   = HAL_GetTick() - s_boot_t0;
    uint32_t time_pct  = (elapsed * 100u) / BOOT_MIN_MS;
    uint32_t pending   = lv_font_provider_preload_pending();
    uint32_t drain_pct;
    uint32_t bar;

    if (time_pct > 100u)
    {
        time_pct = 100u;
    }

    if (s_boot_pending0 == 0u)
    {
        /* GBK (or fallback): no preload to wait for - the bar tracks the
         * mandatory 2 s dwell. */
        drain_pct = 100u;
    }
    else
    {
        uint32_t done = (s_boot_pending0 > pending) ? (s_boot_pending0 - pending)
                                                    : 0u;
        drain_pct = (uint32_t)((done * 100u) / s_boot_pending0);
        if (drain_pct > 100u)
        {
            drain_pct = 100u;
        }
    }

    /* The bar reaches 100 % only when BOTH the 2 s dwell and (for TTF) the
     * preload have completed - whichever is the slower. */
    bar = (time_pct < drain_pct) ? time_pct : drain_pct;
    ui_page_boot_set((uint8_t)bar);

    if ((time_pct >= 100u) && (drain_pct >= 100u))
    {
        /* Fonts are warm (or the engine never needed them): show page 0 and
         * pin its glyphs, then start the periodic rotation. */
        PRINT_LOG("[BOOT] done: pending0=%lu pending=%lu elapsed=%lu engine=%d\r\n",
                  (unsigned long)s_boot_pending0, (unsigned long)pending,
                  (unsigned long)elapsed, (int)lv_font_provider_engine());
        lv_scr_load(s_pages[0]);
        lv_font_provider_on_page_shown();
        s_cur_page = 0U;
        (void)lv_timer_create(page_switch_cb, PAGE_SWITCH_MS, NULL);
        lv_timer_del(timer);
    }
}

/*----------------------------------------------------------------------------*/
/* Public API                                                                 */
/*----------------------------------------------------------------------------*/

void app_ui_create(void)
{
    /* DWT is enabled so the main loop can measure the Chinese rasterisation
     * cost of each switch (cold first paint vs. warm cache hit). */
    dwt_ensure_enabled();

    /* Build the two rotating pages off-screen first so their labels enqueue
     * the glyph preload; the boot page waits for that queue to drain. */
    s_pages[0] = ui_page_info_build();
    PRINT_LOG("[BOOT] after info build: pending=%lu\r\n",
              (unsigned long)lv_font_provider_preload_pending());
    s_pages[1] = ui_page_font_build();
    PRINT_LOG("[BOOT] after font build: pending=%lu\r\n",
              (unsigned long)lv_font_provider_preload_pending());
    s_boot_pending0 = lv_font_provider_preload_pending();

    /* Render each content page once (display flush suppressed) so the first
     * real screen switch only re-paints already-warm objects.  This removes the
     * ~190 ms first-paint penalty the user saw on the font page. */
    ui_warmup_pages();

    lv_obj_t *boot = ui_page_boot_build();
    lv_scr_load(boot);
    ui_page_boot_set_status((lv_font_provider_engine() == FONT_ENGINE_CTF)
                                ? "字库预加载中..." : "系统启动中...");

    s_boot_t0 = HAL_GetTick();
    (void)lv_timer_create(boot_timer_cb, 50, NULL);
}

void app_ui_request_sd_refresh(void)
{
    ui_page_info_request_sd_refresh();
}
