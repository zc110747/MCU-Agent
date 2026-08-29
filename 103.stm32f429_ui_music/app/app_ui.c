/**
  ******************************************************************************
  * @file    app_ui.c
  * @brief   UI framework core for the STM32F429 panel (minimal dark theme).
  *
  *  This file owns the framework: geometry, the shared widget helpers, the
  *  bottom navigation bar, the 2 Hz refresh tick, the page dispatch and the
  *  public API (app_ui.h).  Each page's widgets are built in app/ui/page_*.c
  *  (status / hwinfo / ctrl / rtc); the shared declarations they need live in
  *  app/ui/ui_common.h.
  *
  *  Geometry
  *  --------
  *  Nothing here is hardcoded to 800x480.  The layout is derived from the
  *  resolution the LVGL display was actually registered with, which is the
  *  active GRAM window reported by the LCD driver (480x800 on the NT35510
  *  module this project is built for).  Deriving it means the panel fills the
  *  glass whatever orientation the controller ends up in, and it is the one
  *  place that has to change if LCD_WIDTH / LCD_HEIGHT are ever retuned.
  *
  *  Screen teardown goes through ui_teardown(), which deletes the refresh
  *  timer BEFORE clearing the screen.  Skipping that order would leave the
  *  timer holding lv_obj_t pointers into freed objects (hard fault on the next
  *  tick).  Page switching uses ui_rebuild(), which keeps the timer alive and
  *  only re-creates the widgets.
  ******************************************************************************
  */
#include "ui_common.h"
#include "app_ui.h"   /* public API prototypes (app_ui_create / app_ui_switch_page / ...) */

/* ---- Core-only state (not shared with pages) ------------------------------ */
static lv_timer_t  *s_timer       = NULL;
uint32_t     s_uptime_sec   = 0U;   /* shared with the status page (ui_common.h) */
static uint32_t     s_last_sec_at  = 0U;
static uint8_t      s_built        = 0U;
static uint8_t      s_refresh_req  = 0U;

/* Chevron point sets for the two navigation buttons.  lv_line keeps its own
 * coordinate space, so these are relative to the line object's top-left. */
static const lv_point_t s_chevron_left[3]  = { { 14, 2 }, { 5, 10 }, { 14, 18 } };
static const lv_point_t s_chevron_right[3] = { { 6, 2 }, { 15, 10 }, { 6, 18 } };

/* ---- Shared state (defined here, declared extern in ui_common.h) ---------- */
ui_handles_t s_ui;
int          s_page       = (int)PAGE_STATUS;
lv_coord_t   s_w       = 0;
lv_coord_t   s_h       = 0;
lv_coord_t   s_band_y[3];
lv_coord_t   s_band_h  = 0;
lv_coord_t   s_nav_y   = 0;
uint8_t      s_led_on     = 0U;   /* LED1 (PB0) state shown on ctrl page */
uint8_t      s_beep_on    = 0U;   /* buzzer (PCF8574 P0) state */

/*----------------------------------------------------------------------------*/
/* Helpers (shared with pages via ui_common.h)                                */
/*----------------------------------------------------------------------------*/
lv_obj_t *mk_label(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                   const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);

    lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(lbl, 0, LV_PART_MAIN);
    lv_label_set_text(lbl, text);
    lv_obj_set_pos(lbl, x, y);

    return lbl;
}

lv_obj_t *mk_label_center(lv_obj_t *parent, lv_coord_t y,
                          const lv_font_t *font, uint32_t color,
                          const char *text)
{
    lv_obj_t *lbl = mk_label(parent, 0, y, font, color, text);

    lv_obj_set_width(lbl, s_w);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(lbl, 0, y);

    return lbl;
}

lv_obj_t *make_band(lv_obj_t *parent, lv_coord_t y, lv_coord_t h,
                    const char *title)
{
    lv_obj_t *card = lv_obj_create(parent);

    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, s_w - (2 * UI_PAD), h);
    lv_obj_set_pos(card, UI_PAD, y);
    lv_obj_set_style_bg_color(card, lv_color_hex(COL_HDR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(COL_DIM), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    (void)mk_label(card, 8, 6, &lv_font_gbk_16, COL_TXT, title);

    return card;
}

static void ui_set_screen_bg(lv_obj_t *scr)
{
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr, 0, LV_PART_MAIN);
}

/**
  * @brief  Delete the refresh timer, then wipe the active screen.
  *         Order matters (see the file header).
  */
static void ui_teardown(void)
{
    if (s_timer != NULL)
    {
        lv_timer_del(s_timer);
        s_timer = NULL;
    }
    lv_obj_clean(lv_scr_act());
    (void)memset(&s_ui, 0, sizeof(s_ui));
    s_built = 0U;
}

/**
  * @brief  Recompute the layout from the resolution the display is registered
  *         with.  Done on every rebuild so a resolution change needs no edits.
  */
static void ui_layout(void)
{
    lv_coord_t content_y;
    lv_coord_t content_h;
    int i;

    s_w = (lv_coord_t)lv_disp_get_hor_res(NULL);
    s_h = (lv_coord_t)lv_disp_get_ver_res(NULL);

    if (s_w <= 0) { s_w = 1; }
    if (s_h <= 0) { s_h = 1; }

    content_y = HDR_H + TOP_GAP;
    content_h = s_h - content_y - NAV_H - TOP_GAP;
    if (content_h < (3 * BAND_GAP))
    {
        content_h = 3 * BAND_GAP;
    }

    s_band_h = (lv_coord_t)((content_h - (2 * BAND_GAP)) / 3);
    for (i = 0; i < 3; i++)
    {
        s_band_y[i] = (lv_coord_t)(content_y + (i * (s_band_h + BAND_GAP)));
    }

    s_nav_y = (lv_coord_t)(s_h - NAV_H);
}

/*----------------------------------------------------------------------------*/
/* Navigation bar                                                             */
/*----------------------------------------------------------------------------*/
lv_obj_t *mk_nav_button(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                        const lv_point_t *chevron)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_t *line;

    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, NAV_BTN_W, NAV_BTN_H);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COL_BTN), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(COL_DIM), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    /* The chevron: a 20x20 line object centred in the button.  lv_line clears
     * LV_OBJ_FLAG_CLICKABLE in its constructor, so taps fall through to the
     * button behind it. */
    line = lv_line_create(btn);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, 20, 20);
    lv_obj_set_pos(line, (NAV_BTN_W - 20) / 2, (NAV_BTN_H - 20) / 2);
    lv_obj_set_style_line_width(line, 3, LV_PART_MAIN);
    lv_obj_set_style_line_color(line, lv_color_hex(COL_TXT), LV_PART_MAIN);
    lv_obj_set_style_line_rounded(line, 1, LV_PART_MAIN);
    lv_line_set_points(line, chevron, 3);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);

    return btn;
}

/*----------------------------------------------------------------------------*/
/* Control page buttons                                                       */
/*----------------------------------------------------------------------------*/
static void ctrl_btn_cb(lv_event_t *e);

lv_obj_t *mk_ctrl_button(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                         lv_coord_t w, lv_coord_t h,
                         const char *title, int code)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_t *lbl;

    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COL_BTN), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(COL_DIM), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &lv_font_gbk_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TXT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(lbl, 0, LV_PART_MAIN);
    lv_label_set_text(lbl, title);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    /* Let taps fall through to the button so the CLICKED event is delivered. */
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_event_cb(btn, ctrl_btn_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)code);

    return lbl;
}

static void ctrl_btn_cb(lv_event_t *e)
{
    int code = (int)(intptr_t)lv_event_get_user_data(e);

    LV_UNUSED(e);

    if (code == 1)
    {
        /* LED1 = PB0, low-active: On() drives the pin low. */
        s_led_on = s_led_on ? 0U : 1U;
        if (s_led_on != 0U)
        {
            BSP_LED_On(1);
        }
        else
        {
            BSP_LED_Off(1);
        }
        if (s_ui.p2_led_lbl != NULL)
        {
            lv_label_set_text(s_ui.p2_led_lbl,
                              s_led_on ? "LED 开" : "LED 关");
        }
        if (s_ui.p2_led_state != NULL)
        {
            lv_label_set_text(s_ui.p2_led_state,
                              s_led_on ? "LED 状态  点亮" : "LED 状态  关闭");
        }
    }
    else
    {
        /* Buzzer = PCF8574 P0, low = sound. */
        s_beep_on = s_beep_on ? 0U : 1U;
        BSP_BEEP_Set(s_beep_on);
        if (s_ui.p2_beep_lbl != NULL)
        {
            lv_label_set_text(s_ui.p2_beep_lbl,
                              s_beep_on ? "蜂鸣器 开" : "蜂鸣器 关");
        }
        if (s_ui.p2_beep_state != NULL)
        {
            lv_label_set_text(s_ui.p2_beep_state,
                              s_beep_on ? "蜂鸣器  鸣响" : "蜂鸣器  静音");
        }
    }

    PRINT_LOG("[UI  ] ctrl btn %d -> led=%d beep=%d\r\n",
              code, (int)s_led_on, (int)s_beep_on);
}

/*----------------------------------------------------------------------------*/
/* Data refresh (core: uptime + per-page dispatchers)                         */
/*----------------------------------------------------------------------------*/
static void ui_tick_cb(lv_timer_t *timer)
{
    uint32_t now = HAL_GetTick();

    LV_UNUSED(timer);

    if ((now - s_last_sec_at) >= 1000U)
    {
        s_last_sec_at = now;
        s_uptime_sec++;
    }

    rtc_check_alarm();   /* alarm rings on any page */

    if (s_page == (int)PAGE_STATUS)
    {
        refresh_usb();
        refresh_runtime();

        if (s_refresh_req != 0U)
        {
            refresh_font();
            s_refresh_req = 0U;
        }
    }
    else if (s_page == (int)PAGE_CTRL)
    {
        refresh_ctrl();
    }
    else if (s_page == (int)PAGE_RTC)
    {
        rtc_refresh_clock();
    }
    else
    {
        refresh_hwinfo();
    }
}

/*----------------------------------------------------------------------------*/
/* Page switch                                                                */
/*----------------------------------------------------------------------------*/
static void nav_btn_cb(lv_event_t *e)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(e);

    app_ui_switch_page(delta);
}

static void build_nav(void)
{
    lv_obj_t *scr  = lv_scr_act();
    lv_obj_t *bar;
    lv_obj_t *btn_l;
    lv_obj_t *btn_r;
    lv_coord_t bar_w = (lv_coord_t)(s_w - (2 * UI_PAD));
    lv_coord_t bar_h = (lv_coord_t)(NAV_H - 8);
    lv_coord_t btn_y = (lv_coord_t)((bar_h - NAV_BTN_H) / 2);

    bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, bar_w, bar_h);
    lv_obj_set_pos(bar, UI_PAD, s_nav_y);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COL_HDR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(bar, lv_color_hex(COL_DIM), LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bar, 0, LV_PART_MAIN);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    btn_l = mk_nav_button(bar, 6, btn_y, s_chevron_left);
    lv_obj_add_event_cb(btn_l, nav_btn_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)-1);

    btn_r = mk_nav_button(bar, (lv_coord_t)(bar_w - NAV_BTN_W - 6), btn_y,
                          s_chevron_right);
    lv_obj_add_event_cb(btn_r, nav_btn_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)1);

    s_ui.page_lbl = mk_label(bar, 0, (lv_coord_t)((bar_h - 20) / 2),
                             &lv_font_gbk_16, COL_DIM, "");
    lv_obj_set_width(s_ui.page_lbl,
                     (lv_coord_t)(bar_w - (2 * NAV_BTN_W) - 24));
    lv_obj_set_pos(s_ui.page_lbl, (lv_coord_t)(NAV_BTN_W + 12),
                   (lv_coord_t)((bar_h - 20) / 2));
    lv_obj_set_style_text_align(s_ui.page_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text_fmt(s_ui.page_lbl, "%d / %d",
                          (int)(s_page + 1), (int)UI_PAGE_COUNT);
}

/*----------------------------------------------------------------------------*/
/* Frame builders                                                             */
/*----------------------------------------------------------------------------*/
static void build_header(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_t *hdr;

    hdr = lv_obj_create(scr);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, s_w, HDR_H);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(COL_HDR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, LV_PART_MAIN);
    (void)mk_label_center(hdr, 5, &lv_font_gbk_24, COL_TXT, "STM32F429 信息面板");
}

static void ui_build(void)
{
    lv_obj_t *scr = lv_scr_act();

    ui_layout();
    ui_set_screen_bg(scr);

    build_header();

    if (s_page == (int)PAGE_HWINFO)
    {
        build_page_hwinfo();
    }
    else if (s_page == (int)PAGE_CTRL)
    {
        build_page_ctrl();
    }
    else if (s_page == (int)PAGE_RTC)
    {
        build_page_rtc();
    }
    else
    {
        build_page_status();
    }

    build_nav();

    s_built = 1U;
}

/**
  * @brief  Re-create every widget without touching the refresh timer.
  *         Used by the page switch; the timer keeps running and only refreshes
  *         the labels that belong to the page that is on screen.
  */
static void ui_rebuild(void)
{
    lv_obj_clean(lv_scr_act());
    (void)memset(&s_ui, 0, sizeof(s_ui));
    s_built = 0U;
    ui_build();
}

/*----------------------------------------------------------------------------*/
/* Public API                                                                 */
/*----------------------------------------------------------------------------*/
void app_ui_create(void)
{
    if (s_built == 0U)
    {
        /* Clear any boot/failure screen that may still be on the display. */
        ui_layout();
        lv_obj_clean(lv_scr_act());
        (void)memset(&s_ui, 0, sizeof(s_ui));
        ui_build();
    }

    s_page       = (int)PAGE_STATUS;
    s_uptime_sec = 0U;
    s_last_sec_at = HAL_GetTick();
    s_refresh_req = 1U;   /* re-read font status on first tick */

    refresh_sd();
    refresh_font();
    refresh_usb();
    refresh_runtime();

    if (s_timer == NULL)
    {
        s_timer = lv_timer_create(ui_tick_cb, UI_REFRESH_MS, NULL);
    }
}

void app_ui_show_centered(const char *text)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_t *lbl;

    ui_teardown();
    ui_layout();
    ui_set_screen_bg(scr);

    /* ASCII-only: the glyphs come from the compiled-in ASCII tables, so this
     * screen renders correctly with no font file and no filesystem. */
    lbl = mk_label_center(scr, 0, &lv_font_gbk_24, COL_TXT,
                          (text != NULL) ? text : "");
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
}

void app_ui_show_fault(const char *line1, const char *line2, const char *line3)
{
    if (s_built == 0U)
    {
        ui_layout();
        lv_obj_clean(lv_scr_act());
        (void)memset(&s_ui, 0, sizeof(s_ui));
        ui_build();
    }

    if (s_ui.p0_f1 != NULL)
    {
        lv_label_set_text(s_ui.p0_f1,
                          (line1 != NULL && line1[0] != '\0') ? line1 : "");
    }
    if (s_ui.p0_f2 != NULL)
    {
        lv_label_set_text(s_ui.p0_f2,
                          (line2 != NULL && line2[0] != '\0') ? line2 : "");
    }
    if (s_ui.p0_f3 != NULL)
    {
        lv_label_set_text(s_ui.p0_f3,
                          (line3 != NULL && line3[0] != '\0') ? line3 : "");
    }
}

void app_ui_request_usb_refresh(void)
{
    if (s_built != 0U)
    {
        s_refresh_req = 1U;
    }
}

void app_ui_switch_page(int delta)
{
    int next = s_page + delta;

    /* Cyclic: wrap into [0, UI_PAGE_COUNT) in both directions. */
    next %= (int)UI_PAGE_COUNT;
    if (next < 0)
    {
        next += (int)UI_PAGE_COUNT;
    }

    if (next == s_page)
    {
        return;
    }

    s_page = next;
    ui_rebuild();

    /* Republish the values that the new page shows. */
    if (s_page == (int)PAGE_STATUS)
    {
        refresh_usb();
        refresh_sd();
        refresh_font();
        refresh_runtime();
    }
    else if (s_page == (int)PAGE_CTRL)
    {
        refresh_ctrl();
    }
    else if (s_page == (int)PAGE_RTC)
    {
        /* build_page_rtc() already loaded the edit buffer and clock. */
    }
    else
    {
        refresh_hwinfo();
    }

    PRINT_LOG("[UI  ] page -> %d / %d\r\n",
           (int)(s_page + 1), (int)UI_PAGE_COUNT);
}

int app_ui_page(void)
{
    return s_page;
}
