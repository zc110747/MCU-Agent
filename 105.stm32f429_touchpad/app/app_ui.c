/**
 ******************************************************************************
 * @file    app_ui.c
 * @brief   Trackpad screen: a touch surface that moves a USB HID mouse, plus
 *          three on-glass buttons for the left / middle / right click.
 *
 *  Layout (480x800 portrait)
 *  -------------------------
 *      +----------------------------------+
 *      | USB 触摸板            USB / FPS   |  header  (48 px)
 *      +----------------------------------+
 *      |                                  |
 *      |          touch surface           |  pad     (456x380)
 *      |   (finger motion -> HID X/Y)     |
 *      |                                  |
 *      +----------------------------------+
 *      |  [ 左键 ]  [ 中键 ]  [ 右键 ]     |  buttons (144x96 each)
 *      +----------------------------------+
 *      | USB state / buttons / motion     |  status  (456x196)
 *      +----------------------------------+
 *      | hints                            |  footer
 *      +----------------------------------+
 *
 *  Input model
 *  -----------
 *  trackpad.c owns the pointer: it is fed from touch_task, i.e. from the
 *  T_PEN-driven sampling loop, and turns sample-to-sample deltas into HID
 *  reports.  This file only paints the feedback (dot + trail) from what
 *  trackpad_poll() hands over, so a slow render pass cannot delay the cursor.
 *  LVGL's pointer indev keeps hit-testing the three glass buttons.
 *
 *  The gesture engine still runs (touch_task feeds it) and its events are used
 *  for the counters / the two-finger readout only - no wheel yet, by design.
 *
 *  Everything here runs in the UI task (the only task allowed to touch LVGL).
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "lv_font_gbk.h"
#include "lv_gbk_map.h"

#include "FreeRTOS.h"

#include "app_ui.h"
#include "gesture.h"
#include "sd_card.h"
#include "bsp_key.h"
#include "bsp_lcd.h"
#include "bsp_lcd_text.h"
#include "bsp_touch.h"
#include "bsp_gt9147.h"
#include "bsp_sdio.h"
#include "usb_mouse.h"
#include "trackpad.h"
#include "log.h"

/* -------------------------------------------------------------------------- */
/* Geometry                                                                   */
/* -------------------------------------------------------------------------- */
/*  The NT35510 comes up with a 480x800 (portrait) GRAM window: see
 *  DFT_SCAN_DIR / lcd_scan_dir() in bsp_lcd.c and the boot log line
 *  "[LCD ] active GRAM window 480x800 (panel spec 800x480)".  app_ui_create()
 *  warns when the live window disagrees with these constants. */
#define SCR_W           480
#define SCR_H           800

#define HDR_H           48

/* Touch surface. */
#define PAD_X           12
#define PAD_Y           56
#define PAD_W           (SCR_W - 2 * PAD_X)        /* 456 */
#define PAD_H           380

/* Three glass buttons: left / middle / right. */
#define BTN_Y           448
#define BTN_H           96
#define BTN_GAP         12
#define BTN_W           ((SCR_W - 2 * PAD_X - 2 * BTN_GAP) / 3)   /* 144 */

/* Status card. */
#define STAT_Y          556
#define STAT_H          196

#define FTR_Y           758

/* Cursor dot drawn under the finger, and the trail it leaves behind. */
#define DOT_SIZE        26
#define TRAIL_MAX       64
#define TRAIL_LINGER_MS 600U

/* One small ring per reported contact.  With multi-touch enabled the panel can
 * report up to five at once; drawing every one of them is what makes "did the
 * second finger get through?" answerable by looking at the glass instead of
 * reading a counter. */
#define CT_SIZE         20

/* The trail line spans the whole touch surface, so every update invalidates
 * ~456x380 px = 350 KB of pixels (the NT35510 then needs ~35 ms to push it).
 * Committing it on every LVGL pass therefore pinned the UI at 13 Hz while a
 * finger moved.  The dot is only 26x26 and stays per-pass; the trail is
 * published at this interval instead, which keeps the pump responsive. */
#define TRAIL_COMMIT_MS 60U

/* -------------------------------------------------------------------------- */
/* Palette                                                                    */
/* -------------------------------------------------------------------------- */
#define C_BG            0x0E1116
#define C_CARD          0x141A22
#define C_PAD           0x11161D
#define C_BORDER        0x2A3340
#define C_TEXT          0xE6EDF3
#define C_DIM           0x8B949E
#define C_ACCENT        0x4C9AFF

/* -------------------------------------------------------------------------- */
/* Widgets / state                                                            */
/* -------------------------------------------------------------------------- */
static lv_obj_t *s_scr;
static lv_obj_t *s_pad;
static lv_obj_t *s_dot;
static lv_obj_t *s_trail_line;
static lv_obj_t *s_hint;

/* One ring per contact reported by the controller (see CT_SIZE). */
static lv_obj_t *s_ct_obj[BSP_TOUCH_MAX_POINTS];

static lv_obj_t *s_btn_obj[3];      /* left, middle, right (visual only) */
static const uint8_t k_btn_mask[3] = { USB_MOUSE_BTN_LEFT,
                                       USB_MOUSE_BTN_MIDDLE,
                                       USB_MOUSE_BTN_RIGHT };

static lv_obj_t *s_usb_label;
static lv_obj_t *s_btn_label;
static lv_obj_t *s_move_label;
static lv_obj_t *s_touch_label;
static lv_obj_t *s_gesture_label;
static lv_obj_t *s_sd_label;
static lv_obj_t *s_rate_label;

static lv_point_t s_trail_pts[TRAIL_MAX];
static uint16_t   s_trail_n = 0U;
static uint32_t   s_trail_clear_at = 0U;
static uint32_t   s_trail_commit_at = 0U;

/* A serial command can "tap" a button: hold it for a moment, then release. */
static uint8_t  s_pulse_mask  = 0U;
static uint32_t s_pulse_until = 0U;

static uint32_t s_loop_count = 0U;
static uint32_t s_ui_hz      = 0U;
static uint32_t s_rate_mark  = 0U;
static uint32_t s_stat_mark  = 0U;
static uint32_t s_key_mark   = 0U;

/* -------------------------------------------------------------------------- */
/* Small builders                                                             */
/* -------------------------------------------------------------------------- */
static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

static lv_obj_t *make_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                           lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *card = lv_obj_create(parent);

    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    return card;
}

/* -------------------------------------------------------------------------- */
/* Touch surface feedback                                                     */
/* -------------------------------------------------------------------------- */
static void trail_reset(void)
{
    s_trail_n = 0U;
    s_trail_commit_at = 0U;     /* publish the next point immediately */
    if (s_trail_line != NULL)
    {
        lv_line_set_points(s_trail_line, s_trail_pts, 0);
    }
}

/* Points are collected in the array and published once per poll.  Calling
 * lv_line_set_points() from trail_add() meant up to 24 invalidations of the
 * whole 456x380 line object per pass, which is what dragged the UI pump down
 * to 13 Hz while a finger was moving. */
static void trail_commit(void)
{
    if (s_trail_line != NULL)
    {
        lv_line_set_points(s_trail_line, s_trail_pts, s_trail_n);
    }
}

static void trail_add(int32_t x, int32_t y)
{
    if (s_trail_line == NULL)
    {
        return;
    }

    if (s_trail_n >= TRAIL_MAX)
    {
        /* Drop the oldest sample so the newest 63 remain. */
        memmove(&s_trail_pts[0], &s_trail_pts[1],
                sizeof(lv_point_t) * (TRAIL_MAX - 1U));
        s_trail_n = TRAIL_MAX - 1U;
    }

    s_trail_pts[s_trail_n].x = (lv_coord_t)x;
    s_trail_pts[s_trail_n].y = (lv_coord_t)y;
    s_trail_n++;

    s_trail_clear_at = 0U;
}

static void dot_show(int32_t x, int32_t y)
{
    if (s_dot == NULL)
    {
        return;
    }

    lv_obj_set_pos(s_dot, (lv_coord_t)(x - DOT_SIZE / 2), (lv_coord_t)(y - DOT_SIZE / 2));
    lv_obj_clear_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
}

static void dot_hide(void)
{
    if (s_dot != NULL)
    {
        lv_obj_add_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief  Paint the pointer feedback that trackpad.c produced.
 *
 *  The motion itself is NOT computed here any more: trackpad_feed() runs in the
 *  touch task and pushes HID deltas the moment the controller answers, so the
 *  cursor no longer waits for an LVGL render pass (that pump measured 84 Hz on
 *  this board, with 12..40 ms of jitter, which is exactly what "not following
 *  the finger" felt like).  This function only drains the visual state, which
 *  may lag without anybody noticing.
 */
static void poll_trackpad(uint32_t now)
{
    trackpad_visual_t vis;
    uint8_t i;

    /* Fade the trail out after the finger lifts. */
    if ((s_trail_n > 0U) && (s_trail_clear_at != 0U)
        && ((int32_t)(now - s_trail_clear_at) >= 0))
    {
        trail_reset();
        s_trail_clear_at = 0U;
    }

    trackpad_poll(&vis);

    if (vis.started != 0U)
    {
        trail_reset();
    }

    for (i = 0U; i < vis.n; i++)
    {
        trail_add(vis.tx[i], vis.ty[i]);
    }

    /* Publish the accumulated trail on a timer, not on every point: see
     * TRAIL_COMMIT_MS.  A release or a new contact commits immediately so the
     * line never lags behind the state change. */
    if ((vis.n > 0U)
        && ((vis.started != 0U) || (vis.ended != 0U)
            || ((int32_t)(now - s_trail_commit_at) >= 0)))
    {
        trail_commit();
        s_trail_commit_at = now + TRAIL_COMMIT_MS;
    }

    if (vis.active != 0U)
    {
        dot_show(vis.x, vis.y);
    }
    else if (vis.ended != 0U)
    {
        dot_hide();
        s_trail_clear_at = now + TRAIL_LINGER_MS;
    }
    else
    {
        /* idle */
    }
}

/* -------------------------------------------------------------------------- */
/* Per-contact rings                                                          */
/* -------------------------------------------------------------------------- */
/**
 * @brief  Draw one ring per contact of the newest frame.
 *
 *  bsp_touch_get_points() returns the whole frame (the touch task publishes it
 *  from the T_PEN-driven poll), so this is pure painting: no I2C, no gesture
 *  logic.  Canvas coordinates are converted to pad-relative ones because the
 *  rings are children of the touch surface card.
 */
static void poll_contacts(void)
{
    bsp_touch_point_t pts[BSP_TOUCH_MAX_POINTS];
    uint8_t n;
    uint8_t i;

    if (s_pad == NULL)
    {
        return;
    }

    n = bsp_touch_get_points(pts, (uint8_t)BSP_TOUCH_MAX_POINTS);

    for (i = 0U; i < (uint8_t)BSP_TOUCH_MAX_POINTS; i++)
    {
        lv_obj_t *ring = s_ct_obj[i];

        if (ring == NULL)
        {
            continue;
        }

        if (i >= n)
        {
            lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        lv_obj_set_pos(ring,
                       (lv_coord_t)((int32_t)pts[i].x - PAD_X - (CT_SIZE / 2)),
                       (lv_coord_t)((int32_t)pts[i].y - PAD_Y - (CT_SIZE / 2)));
        lv_obj_clear_flag(ring, LV_OBJ_FLAG_HIDDEN);
    }
}

/* -------------------------------------------------------------------------- */
/* Glass buttons                                                              */
/* -------------------------------------------------------------------------- */
static void key_visual(uint8_t mask, int pressed)
{
    uint8_t i;

    for (i = 0U; i < 3U; i++)
    {
        if ((s_btn_obj[i] != NULL) && (k_btn_mask[i] == mask))
        {
            if (pressed != 0)
            {
                lv_obj_add_state(s_btn_obj[i], LV_STATE_PRESSED);
            }
            else
            {
                lv_obj_clear_state(s_btn_obj[i], LV_STATE_PRESSED);
            }
        }
    }
}

static void on_key_pressed(lv_event_t *e)
{
    uint8_t mask = (uint8_t)(uintptr_t)lv_event_get_user_data(e);

    usb_mouse_set_button(mask, 1);
}

static void on_key_released(lv_event_t *e)
{
    uint8_t mask = (uint8_t)(uintptr_t)lv_event_get_user_data(e);

    usb_mouse_set_button(mask, 0);
}

static lv_obj_t *make_key_button(lv_obj_t *parent, const char *txt, const char *tag,
                                 lv_coord_t x, uint8_t mask)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_t *label;

    lv_obj_set_pos(btn, x, BTN_Y);
    lv_obj_set_size(btn, BTN_W, BTN_H);
    lv_obj_set_style_bg_color(btn, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(C_ACCENT), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    /* PRESSED / RELEASED give a true "hold" semantic (drag with the button
     * down); PRESS_LOST covers the finger sliding off the button. */
    lv_obj_add_event_cb(btn, on_key_pressed,  LV_EVENT_PRESSED,  (void *)(uintptr_t)mask);
    lv_obj_add_event_cb(btn, on_key_released, LV_EVENT_RELEASED, (void *)(uintptr_t)mask);
    lv_obj_add_event_cb(btn, on_key_released, LV_EVENT_PRESS_LOST, (void *)(uintptr_t)mask);

    label = make_label(btn, &lv_font_gbk_24, C_TEXT);
    lv_label_set_text(label, txt);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -8);

    label = make_label(btn, &lv_font_gbk_12, C_DIM);
    lv_label_set_text(label, tag);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 22);

    return btn;
}

/* -------------------------------------------------------------------------- */
/* Status                                                                     */
/* -------------------------------------------------------------------------- */
static void set_gesture_text(const char *txt)
{
    if (s_gesture_label != NULL)
    {
        lv_label_set_text(s_gesture_label, txt);
    }
}

static void handle_event(const gesture_evt_t *ev)
{
    switch (ev->type)
    {
    case GESTURE_EVT_TAP:
        /* The click itself is emitted by trackpad.c (it is the layer that knows
         * the pad area, and it is the one that must not fire when the contact
         * started on a glass key).  This is only the readout. */
        set_gesture_text("单指轻点: 左键");
        break;

    case GESTURE_EVT_TAP2:
        set_gesture_text("双指轻点");
        break;

    case GESTURE_EVT_TAP3:
        set_gesture_text("三指轻点");
        break;

    case GESTURE_EVT_SCROLL:
        /* Wheel output is intentionally not implemented yet (requirement:
         * scroll comes later), so the readout is all that happens here. */
        set_gesture_text((ev->d >= 0) ? "双指下滑 (滚轮未启用)"
                                     : "双指上滑 (滚轮未启用)");
        break;

    case GESTURE_EVT_PAN:
        set_gesture_text((ev->d >= 0) ? "双指右滑 (滚轮未启用)"
                                     : "双指左滑 (滚轮未启用)");
        break;

    default:
        break;
    }
}

static void refresh_stats(void)
{
    gesture_status_t   st;
    usb_mouse_status_t ms;

    gesture_get_status(&st);
    usb_mouse_get_status(&ms);

    /* USB link state: single accent colour scheme, no red/green status lights. */
    lv_label_set_text(s_usb_label, (ms.mounted != 0U) ? "HID 鼠标已连接"
                                                      : "USB 未连接");

    lv_label_set_text_fmt(s_btn_label, "左键 %s   中键 %s   右键 %s",
                          ((ms.buttons & USB_MOUSE_BTN_LEFT)   != 0U) ? "按下" : "松开",
                          ((ms.buttons & USB_MOUSE_BTN_MIDDLE) != 0U) ? "按下" : "松开",
                          ((ms.buttons & USB_MOUSE_BTN_RIGHT)  != 0U) ? "按下" : "松开");

    lv_label_set_text_fmt(s_move_label, "位移 X %ld  Y %ld    报告 %lu",
                          (long)ms.x_total, (long)ms.y_total,
                          (unsigned long)ms.reports);

    lv_label_set_text_fmt(s_touch_label, "触点 %u/%u   单指 %lu   多指 %lu   滚动 %lu",
                          (unsigned int)st.fingers,
                          (unsigned int)bsp_touch_max_points(),
                          (unsigned long)st.taps,
                          (unsigned long)st.multi_taps,
                          (unsigned long)st.scrolls);
}

/* -------------------------------------------------------------------------- */
/* Screen construction                                                        */
/* -------------------------------------------------------------------------- */
void app_ui_boot(const char *ascii_msg)
{
    static const char *s_shown = NULL;
    lv_obj_t *label;

    /* The UI task calls this from its probe loop; redrawing the splash on every
     * pass would flicker, so only rebuild it when the text actually changes. */
    if ((s_shown != NULL) && (strcmp(s_shown, ascii_msg) == 0))
    {
        return;
    }
    s_shown = ascii_msg;

    s_scr = lv_scr_act();
    lv_obj_clean(s_scr);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(C_BG), 0);

    /* The widgets just went away with lv_obj_clean(). */
    s_pad           = NULL;
    s_dot           = NULL;
    s_trail_line    = NULL;
    s_hint          = NULL;
    s_usb_label     = NULL;
    s_btn_label     = NULL;
    s_move_label    = NULL;
    s_touch_label   = NULL;
    s_gesture_label = NULL;
    s_sd_label      = NULL;
    s_rate_label    = NULL;
    s_btn_obj[0]    = NULL;
    s_btn_obj[1]    = NULL;
    s_btn_obj[2]    = NULL;
    (void)memset(s_ct_obj, 0, sizeof(s_ct_obj));
    s_trail_n       = 0U;
    s_pulse_mask    = 0U;
    trackpad_clear_area();

    label = lv_label_create(s_scr);
    lv_label_set_text(label, ascii_msg);
    lv_obj_set_style_text_font(label, &lv_font_gbk_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(C_DIM), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -12);

    label = lv_label_create(s_scr);
    lv_label_set_text(label, "STM32F429  USB HID mouse");
    lv_obj_set_style_text_font(label, &lv_font_gbk_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(C_BORDER), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 16);
}

void app_ui_create(void)
{
    lv_obj_t *card;
    lv_obj_t *label;
    uint8_t   i;
    uint32_t total_mb = 0U;
    uint32_t used_mb  = 0U;

    s_scr = lv_scr_act();
    lv_obj_clean(s_scr);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(C_BG), 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* The constants assume the 480x800 portrait window this panel reports.
     * Say so loudly if that ever changes, instead of drawing off-screen. */
    if ((LCD_Width != SCR_W) || (LCD_Height != SCR_H))
    {
        PRINT_LOG("[UI  ] WARN layout is %dx%d but the panel is %lux%lu\r\n",
                  SCR_W, SCR_H, (unsigned long)LCD_Width, (unsigned long)LCD_Height);
    }

    s_trail_n    = 0U;
    s_pulse_mask = 0U;

    /* Tell the pointer engine where the touch surface is.  Until this call
     * every sample is ignored (splash / boot screens). */
    trackpad_set_area(PAD_X, PAD_Y, PAD_W, PAD_H);

    /* ---- Header ---------------------------------------------------------- */
    label = make_label(s_scr, &lv_font_gbk_24, C_TEXT);
    lv_label_set_text(label, "USB 触摸板");
    lv_obj_set_pos(label, 16, 8);

    s_sd_label = make_label(s_scr, &lv_font_gbk_12, C_DIM);
    lv_obj_align(s_sd_label, LV_ALIGN_TOP_RIGHT, -16, 10);

    s_rate_label = make_label(s_scr, &lv_font_gbk_12, C_DIM);
    lv_obj_align(s_rate_label, LV_ALIGN_TOP_RIGHT, -16, 28);

    /* ---- Touch surface --------------------------------------------------- */
    s_pad = make_card(s_scr, PAD_X, PAD_Y, PAD_W, PAD_H);
    lv_obj_set_style_bg_color(s_pad, lv_color_hex(C_PAD), 0);
    lv_obj_set_style_pad_all(s_pad, 0, 0);
    lv_obj_clear_flag(s_pad, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_hint = make_label(s_pad, &lv_font_gbk_16, C_BORDER);
    lv_label_set_text(s_hint, "在此滑动 = 移动鼠标");
    lv_obj_align(s_hint, LV_ALIGN_CENTER, 0, 0);

    s_trail_line = lv_line_create(s_pad);
    lv_obj_set_style_line_width(s_trail_line, 4, 0);
    lv_obj_set_style_line_color(s_trail_line, lv_color_hex(C_ACCENT), 0);
    lv_obj_set_style_line_rounded(s_trail_line, true, 0);
    lv_obj_clear_flag(s_trail_line, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_trail_line, 0, 0);

    s_dot = lv_obj_create(s_pad);
    lv_obj_set_size(s_dot, DOT_SIZE, DOT_SIZE);
    lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(C_ACCENT), 0);
    lv_obj_set_style_bg_opa(s_dot, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_dot, 2, 0);
    lv_obj_set_style_border_color(s_dot, lv_color_hex(C_TEXT), 0);
    lv_obj_clear_flag(s_dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_dot, LV_OBJ_FLAG_HIDDEN);

    /* Contact rings: hollow, one per finger, so a second (third, ...) contact
     * is visible even while the pointer dot stays frozen in two-finger mode. */
    for (i = 0U; i < (uint8_t)BSP_TOUCH_MAX_POINTS; i++)
    {
        lv_obj_t *ring = lv_obj_create(s_pad);

        s_ct_obj[i] = ring;
        lv_obj_set_size(ring, CT_SIZE, CT_SIZE);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(ring, 2, 0);
        lv_obj_set_style_border_color(ring, lv_color_hex(C_TEXT), 0);
        lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN);
    }

    /* ---- Glass buttons: left / middle / right ---------------------------- */
    s_btn_obj[0] = make_key_button(s_scr, "左键", "LEFT",   PAD_X, USB_MOUSE_BTN_LEFT);
    s_btn_obj[1] = make_key_button(s_scr, "中键", "MIDDLE", PAD_X + (BTN_W + BTN_GAP),
                                   USB_MOUSE_BTN_MIDDLE);
    s_btn_obj[2] = make_key_button(s_scr, "右键", "RIGHT",  PAD_X + 2 * (BTN_W + BTN_GAP),
                                   USB_MOUSE_BTN_RIGHT);

    /* ---- Status card ----------------------------------------------------- */
    card = make_card(s_scr, PAD_X, STAT_Y, PAD_W, STAT_H);

    label = make_label(card, &lv_font_gbk_16, C_DIM);
    lv_label_set_text(label, "USB 状态");
    lv_obj_set_pos(label, 0, 0);

    s_usb_label = make_label(card, &lv_font_gbk_24, C_TEXT);
    lv_label_set_text(s_usb_label, "USB 未连接");
    lv_obj_set_pos(s_usb_label, 0, 22);

    s_btn_label = make_label(card, &lv_font_gbk_16, C_TEXT);
    lv_obj_set_pos(s_btn_label, 0, 58);
    lv_obj_set_width(s_btn_label, PAD_W - 16);

    s_move_label = make_label(card, &lv_font_gbk_16, C_TEXT);
    lv_obj_set_pos(s_move_label, 0, 86);
    lv_obj_set_width(s_move_label, PAD_W - 16);

    s_touch_label = make_label(card, &lv_font_gbk_16, C_TEXT);
    lv_obj_set_pos(s_touch_label, 0, 114);
    lv_obj_set_width(s_touch_label, PAD_W - 16);

    s_gesture_label = make_label(card, &lv_font_gbk_16, C_ACCENT);
    lv_label_set_text(s_gesture_label, "等待触摸…");
    lv_obj_set_pos(s_gesture_label, 0, 146);
    lv_obj_set_width(s_gesture_label, PAD_W - 16);

    /* ---- Footer ---------------------------------------------------------- */
    if (sd_card_is_ready() != 0)
    {
        if (sd_card_usage_mb(&total_mb, &used_mb) == RT_OK)
        {
            lv_label_set_text_fmt(s_sd_label, "SD 0: %lu MB / %lu MB",
                                  (unsigned long)used_mb, (unsigned long)total_mb);
        }
        else
        {
            lv_label_set_text_fmt(s_sd_label, "SD 0: %lu MB",
                                  (unsigned long)sd_card_capacity_mb());
        }
    }
    else
    {
        lv_label_set_text(s_sd_label, "SD: 无卡 (仅 ASCII)");
    }

    label = make_label(s_scr, &lv_font_gbk_12, C_DIM);
    lv_label_set_text(label, "触摸板 = 鼠标移动   下方三键 = 左/中/右\n"
                             "串口 l/m/r 模拟按键  x/y 移动  s 状态");
    lv_obj_set_pos(label, 12, FTR_Y);

    refresh_stats();
    set_gesture_text("等待触摸…");
}

/* -------------------------------------------------------------------------- */
/* Poll                                                                       */
/* -------------------------------------------------------------------------- */
static void poll_pulse(uint32_t now)
{
    if (s_pulse_mask == 0U)
    {
        return;
    }

    if ((int32_t)(now - s_pulse_until) >= 0)
    {
        usb_mouse_set_button(s_pulse_mask, 0);
        key_visual(s_pulse_mask, 0);
        s_pulse_mask = 0U;
    }
}

static void poll_keys(void)
{
    uint32_t now = HAL_GetTick();

    if ((now - s_key_mark) < BSP_KEY_SCAN_PERIOD_MS)
    {
        return;
    }
    s_key_mark = now;

    bsp_key_poll();

    /* KEY0 clears the trail and the on-screen clue; the other physical keys no
     * longer emit mouse clicks - the three glass buttons below the pad do
     * that now (requirement). */
    if (bsp_key_edge(BSP_KEY_0) != 0U)
    {
        trail_reset();
        dot_hide();
        set_gesture_text("已清除轨迹");
    }
}

void app_ui_poll(void)
{
    gesture_evt_t ev;
    uint32_t now;

    /* Nothing to drive yet (splash still up or screen not built). */
    if ((s_scr == NULL) || (s_gesture_label == NULL))
    {
        return;
    }

    /* Drain everything the touch task produced since the last pass. */
    while (gesture_poll(&ev) != 0)
    {
        handle_event(&ev);
    }

    now = HAL_GetTick();

    poll_trackpad(now);
    poll_contacts();
    poll_pulse(now);
    poll_keys();

    s_loop_count++;

    if ((now - s_rate_mark) >= 1000U)
    {
        uint32_t rate = s_loop_count * 1000U / (now - s_rate_mark);

        s_ui_hz = rate;
        lv_label_set_text_fmt(s_rate_label, "UI %lu Hz", (unsigned long)rate);
        s_loop_count = 0U;
        s_rate_mark  = now;
    }

    if ((now - s_stat_mark) >= 250U)
    {
        s_stat_mark = now;
        refresh_stats();
    }
}

/* -------------------------------------------------------------------------- */
/* Serial command channel (USART1 console)                                    */
/* -------------------------------------------------------------------------- */
/*  One character per command, executed from the UI task so every LVGL call
 *  stays on the task that owns the display.  l / m / r are the useful ones for
 *  bringing up the HID link without touching the glass: they tap the matching
 *  button for 150 ms, so "does the host see a click?" can be answered from a
 *  terminal.                                                                  */

#define PULSE_MS   150U

#if PRINT_LOG_ENABLE
/* ---- Font self-test ------------------------------------------------------ *
 * A glyph that is absent from the SD .FON file is drawn as a blank box, which
 * is exactly what "中文没显示全" looks like on the glass.  This walks every
 * string the UI can put on screen, converts it to GBK and verifies that a
 * non-empty bitmap can actually be read for each size in use.  Distinguishes
 * "the character is missing" from "the layout clipped it". */
#define FONT_CHECK_BUF  128U

static const char *k_ui_text[] =
{
    "USB 触摸板",
    "在此滑动 = 移动鼠标",
    "左键",
    "中键",
    "右键",
    "USB 状态",
    "HID 鼠标已连接",
    "USB 未连接",
    "左键 松开   中键 松开   右键 松开",
    "位移 X 0  Y 0    报告 0",
    "触点 1/5   单指 0   多指 0   滚动 0",
    "等待触摸…",
    "双指轻点",
    "三指轻点",
    "双指下滑 (滚轮未启用)",
    "双指上滑 (滚轮未启用)",
    "已清除轨迹",
    "串口: 左键",
    "SD: 无卡 (仅 ASCII)",
    "触摸板 = 鼠标移动   下方三键 = 左/中/右",
    "串口 l/m/r 模拟按键  x/y 移动  s 状态",
};

static const char *utf8_next(const char *s, uint32_t *cp)
{
    const uint8_t *p = (const uint8_t *)s;

    if (p[0] < 0x80U)
    {
        *cp = p[0];
        return s + 1;
    }
    if ((p[0] & 0xE0U) == 0xC0U)
    {
        *cp = ((uint32_t)(p[0] & 0x1FU) << 6) | (uint32_t)(p[1] & 0x3FU);
        return s + 2;
    }
    if ((p[0] & 0xF0U) == 0xE0U)
    {
        *cp = ((uint32_t)(p[0] & 0x0FU) << 12)
            | ((uint32_t)(p[1] & 0x3FU) << 6)
            | (uint32_t)(p[2] & 0x3FU);
        return s + 3;
    }
    *cp = p[0];
    return s + 1;
}

static void font_check(void)
{
    static uint8_t bmp[FONT_CHECK_BUF];
    pFONT *sizes[3] = { &CH_TEXT_Font12, &CH_TEXT_Font16, &CH_TEXT_Font24 };
    static const uint16_t px[3] = { 12U, 16U, 24U };
    uint32_t missing_map = 0U;
    uint32_t missing_glyph = 0U;
    uint32_t checked = 0U;
    uint32_t shown = 0U;
    uint32_t t;
    uint32_t f;

    /* A complete GBK dot-matrix file holds 126 lead bytes (0x81..0xFE) x 190
     * trail slots, i.e. 23940 glyphs.  Printing the real size first separates
     * "the card holds a trimmed font" from "the lookup arithmetic is wrong"
     * without another round trip. */
    for (f = 0U; f < 3U; f++)
    {
        uint32_t have = lcd_driver_font_file_size(px[f]);
        uint32_t expect = 126U * 190U * sizes[f]->Sizes;

        PRINT_LOG("[FONT] %upx file=%lu B, full GBK=%lu B%s\r\n",
                  (unsigned int)px[f], (unsigned long)have, (unsigned long)expect,
                  (have < expect) ? "  <-- TRIMMED" : "");
    }

    for (t = 0U; t < sizeof(k_ui_text) / sizeof(k_ui_text[0]); t++)
    {
        const char *p = k_ui_text[t];
        uint32_t cp;

        while (*p != '\0')
        {
            uint16_t gbk;

            p = utf8_next(p, &cp);
            if (cp < 0x80U)
            {
                continue;
            }

            checked++;

            gbk = lv_gbk_from_unicode(cp);
            if (gbk == 0U)
            {
                missing_map++;
                PRINT_LOG("[FONT] no GBK mapping: U+%04lX\r\n", (unsigned long)cp);
                continue;
            }

            for (f = 0U; f < 3U; f++)
            {
                uint8_t  code[2];
                uint8_t  any = 0U;
                uint32_t i;

                code[0] = (uint8_t)(gbk >> 8);
                code[1] = (uint8_t)(gbk & 0xFFU);

                if (lcd_driver_get_hzmat_raw(code, bmp, sizes[f]) != RT_OK)
                {
                    uint32_t off = 0U;
                    uint16_t got = 0U;
                    uint8_t  slot = 0U;
                    int8_t   rd = 0;
                    uint8_t  err = lcd_driver_font_last_error(&off, &got, &slot, &rd);

                    missing_glyph++;
                    if (shown < 8U)
                    {
                        shown++;
                        PRINT_LOG("[FONT] %upx U+%04lX GBK=%02X%02X FAIL seek=%d "
                                  "read=%d off=%lu got=%u/%u slot=%u\r\n",
                                  (unsigned int)px[f], (unsigned long)cp, code[0], code[1],
                                  (int)err, (int)rd, (unsigned long)off, (unsigned int)got,
                                  (unsigned int)sizes[f]->Sizes, (unsigned int)slot);
                    }
                    continue;
                }

                for (i = 0U; i < sizes[f]->Sizes; i++)
                {
                    if (bmp[i] != 0U)
                    {
                        any = 1U;
                        break;
                    }
                }

                if (any == 0U)
                {
                    missing_glyph++;
                    if (shown < 12U)
                    {
                        shown++;
                        PRINT_LOG("[FONT] %upx U+%04lX GBK=%02X%02X blank\r\n",
                                  (unsigned int)px[f], (unsigned long)cp, code[0], code[1]);
                    }
                }
            }
        }
    }

    PRINT_LOG("[FONT] check done: %lu CJK chars, map-miss=%lu glyph-miss=%lu\r\n",
              (unsigned long)checked, (unsigned long)missing_map,
              (unsigned long)missing_glyph);
}

/* Dump the contacts of the newest frame.  This is the "did the panel really
 * report two fingers?" evidence: one line per contact, with its track id. */
static void report_points(void)
{
    bsp_touch_point_t pts[BSP_TOUCH_MAX_POINTS];
    uint8_t n = bsp_touch_get_points(pts, (uint8_t)BSP_TOUCH_MAX_POINTS);
    uint8_t i;

    PRINT_LOG("[TPTS] n=%u of %u  peak=%lu multi_frames=%lu\r\n",
              (unsigned int)n, (unsigned int)bsp_touch_max_points(),
              (unsigned long)bsp_touch_peak_points(),
              (unsigned long)bsp_touch_multi_frames());

    for (i = 0U; i < n; i++)
    {
        PRINT_LOG("[TPTS]   #%u id=%u x=%u y=%u size=%u\r\n",
                  (unsigned int)i, (unsigned int)pts[i].id,
                  (unsigned int)pts[i].x, (unsigned int)pts[i].y,
                  (unsigned int)pts[i].size);
    }
}

/**
 * @brief  Hex dump of the raw coordinate block.
 *
 *  Printed twice: the bytes of the last frame the driver parsed, and a live
 *  read done right now.  Together they pin down whether a wrong coordinate
 *  comes from the chip (both dumps agree) or from the parsing / frame handling
 *  (only the live read looks sane).  Layout, 8 bytes per contact starting at
 *  the track id in 0x814F: [id, xL, xH, yL, yH, sizeL, sizeH, rsv].
 */
static void report_block(void)
{
    uint8_t buf[GT9147_MAX_POINTS * GT9147_POINT_STRIDE];
    uint8_t n;
    uint8_t i;
    char    hex[3U * 40U + 1U];
    size_t  off;

    n = bsp_gt9147_last_block(buf, (uint8_t)sizeof(buf));
    off = 0U;
    for (i = 0U; (i < n) && ((off + 4U) < sizeof(hex)); i++)
    {
        (void)snprintf(&hex[off], sizeof(hex) - off, "%02X ", buf[i]);
        off += 3U;
    }
    hex[off] = '\0';
    PRINT_LOG("[BLK ] last n=%u: %s\r\n", (unsigned int)n, hex);

    if (bsp_gt9147_dump_block(buf, 8U) == 0)
    {
        off = 0U;
        for (i = 0U; i < 8U; i++)
        {
            (void)snprintf(&hex[off], sizeof(hex) - off, "%02X ", buf[i]);
            off += 3U;
        }
        hex[off] = '\0';
        PRINT_LOG("[BLK ] now  (0x814F): %s\r\n", hex);
    }
    else
    {
        PRINT_LOG("[BLK ] now  (0x814F): read FAILED\r\n");
    }
}

static void report_status(void)
{
    gesture_status_t   st;
    usb_mouse_status_t ms;
    trackpad_stats_t   tp;
    uint32_t total_mb = 0U;
    uint32_t used_mb  = 0U;
    uint32_t f_new = 0U;
    uint32_t f_empty = 0U;
    uint32_t f_release = 0U;
    uint16_t touch_x = 0U;
    uint16_t touch_y = 0U;
    uint16_t raw_x   = 0U;
    uint16_t raw_y   = 0U;
    uint8_t  touch_pressed = 0U;

    gesture_get_status(&st);
    usb_mouse_get_status(&ms);
    trackpad_get_stats(&tp);
    bsp_touch_frame_stats(&f_new, &f_empty, &f_release);
    bsp_touch_get(&touch_x, &touch_y, &touch_pressed);
    bsp_touch_raw(&raw_x, &raw_y);
    (void)sd_card_usage_mb(&total_mb, &used_mb);

    PRINT_LOG("[USB ] mounted=%u suspended=%u buttons=0x%02X reports=%lu\r\n",
              (unsigned int)ms.mounted, (unsigned int)ms.suspended,
              (unsigned int)ms.buttons, (unsigned long)ms.reports);
    PRINT_LOG("[PERF] ui=%luHz touch_scan=%lu us=%lu send=%lu ep_busy=%lu lat=%lums clk=%luMHz\r\n",
              (unsigned long)s_ui_hz,
              (unsigned long)bsp_touch_scan_count(),
              (unsigned long)bsp_touch_scan_avg_us(),
              (unsigned long)ms.tries, (unsigned long)ms.endpoint_busy,
              (unsigned long)ms.lat_avg_ms,
              (unsigned long)(SystemCoreClock / 1000000U));
    PRINT_LOG("[PAD ] feeds=%lu moves=%lu clicks=%lu taps=%lu outside=%lu "
              "drop=%lu multi=%lu reanchor=%lu\r\n",
              (unsigned long)tp.feeds, (unsigned long)tp.moves,
              (unsigned long)tp.clicks, (unsigned long)tp.taps,
              (unsigned long)tp.outside,
              (unsigned long)tp.dropped, (unsigned long)tp.multi,
              (unsigned long)tp.reanchor);
    PRINT_LOG("[MOVE] x=%ld y=%ld\r\n", (long)ms.x_total, (long)ms.y_total);
    PRINT_LOG("[SD  ] %s %lu/%lu MB\r\n",
              (sd_card_is_ready() != 0) ? "0: mounted" : "0: absent",
              (unsigned long)used_mb, (unsigned long)total_mb);
    PRINT_LOG("[FONT] src=%s mask=0x%02lX\r\n",
              lcd_driver_font_source(),
              (unsigned long)lcd_driver_font_status());
    PRINT_LOG("[TOUCH] irq=%lu ready=%d fingers=%u maxp=%u peak=%lu multi=%lu st=0x%02X "
              "raw=%u,%u pos=%u,%u clamp=%lu press=%u frame=%lu empty=%lu release=%lu\r\n",
              (unsigned long)bsp_touch_irq_count(), bsp_touch_is_ready(),
              (unsigned int)st.fingers,
              (unsigned int)bsp_touch_max_points(),
              (unsigned long)bsp_touch_peak_points(),
              (unsigned long)bsp_touch_multi_frames(),
              (unsigned int)bsp_gt9147_last_status(),
              (unsigned int)raw_x, (unsigned int)raw_y,
              (unsigned int)touch_x, (unsigned int)touch_y,
              (unsigned long)bsp_touch_clamp_count(),
              (unsigned int)touch_pressed,
              (unsigned long)f_new, (unsigned long)f_empty,
              (unsigned long)f_release);
    report_points();
    PRINT_LOG("[GEST] press=%lu tap=%lu multi=%lu scroll=%lu pan=%lu drop=%lu\r\n",
              (unsigned long)st.presses, (unsigned long)st.taps,
              (unsigned long)st.multi_taps, (unsigned long)st.scrolls,
              (unsigned long)st.pans, (unsigned long)st.dropped);
    PRINT_LOG("[UI  ] screen=%d pad=%d\r\n",
              (s_scr != NULL) ? 1 : 0, (s_pad != NULL) ? 1 : 0);
}

static void report_help(void)
{
    PRINT_LOG("[CMD ] l/m/r=tap button  x/y=nudge pointer  c=clear  p=points  "
              "b=raw block  d=raw trace  s=status  f=font check  h=help\r\n");
}
#endif /* PRINT_LOG_ENABLE */

static void tap_button(uint8_t mask, const char *name)
{
    usb_mouse_set_button(mask, 1);
    key_visual(mask, 1);

    s_pulse_mask  = mask;
    s_pulse_until = HAL_GetTick() + PULSE_MS;

    set_gesture_text(name);
}

void app_ui_command(char c)
{
    if ((s_scr == NULL) || (s_gesture_label == NULL))
    {
        PRINT_LOG("[CMD ] screen not ready\r\n");
        return;
    }

    switch (c)
    {
    case 'l':
    case 'L':
        tap_button(USB_MOUSE_BTN_LEFT, "串口: 左键");
        PRINT_LOG("[CMD ] left click\r\n");
        break;

    case 'm':
    case 'M':
        tap_button(USB_MOUSE_BTN_MIDDLE, "串口: 中键");
        PRINT_LOG("[CMD ] middle click\r\n");
        break;

    case 'r':
    case 'R':
        tap_button(USB_MOUSE_BTN_RIGHT, "串口: 右键");
        PRINT_LOG("[CMD ] right click\r\n");
        break;

    case 'x':
    case 'X':
        /* Nudge the pointer right / down: lets a host-side script prove that
         * the HID report path really moves the cursor without touching the
         * glass.  10 x 4 px of "finger" motion, scaled by the acceleration
         * curve inside usb_mouse_move(). */
        for (uint8_t i = 0U; i < 10U; i++)
        {
            usb_mouse_move(4, 0);
        }
        set_gesture_text("串口: 右移");
        PRINT_LOG("[CMD ] move right\r\n");
        break;

    case 'y':
    case 'Y':
        for (uint8_t i = 0U; i < 10U; i++)
        {
            usb_mouse_move(0, 4);
        }
        set_gesture_text("串口: 下移");
        PRINT_LOG("[CMD ] move down\r\n");
        break;

    case 'c':
    case 'C':
        trail_reset();
        dot_hide();
        set_gesture_text("串口: 清轨迹");
        PRINT_LOG("[CMD ] trail cleared\r\n");
        break;

#if PRINT_LOG_ENABLE
    case 'p':
    case 'P':
        /* Snapshot of the newest contact frame.  Send it while two or three
         * fingers are on the glass to see what the controller really reports. */
        report_points();
        break;

    case 'b':
    case 'B':
        /* Hex dump of the raw coordinate block (see report_block). */
        report_block();
        break;

    case 'd':
    case 'D':
    {
        /* Toggle the per-frame raw trace.  Bring-up only: while it is on the
         * console gets one line per contact every 300 ms as long as a finger
         * is down, which buries the command replies. */
        uint8_t on = bsp_gt9147_trace((bsp_gt9147_trace(0xFFU) == 0U) ? 1U : 0U);

        PRINT_LOG("[CMD ] raw trace %s\r\n", (on != 0U) ? "ON" : "OFF");
        break;
    }

    case 's':
    case 'S':
    case '?':
        report_status();
        break;

    case 'h':
    case 'H':
        report_help();
        break;

    case 'f':
    case 'F':
        /* Verify every CJK glyph the UI can show exists in the SD .FON files.
         * Blocks for a moment (one seek+read per character and size). */
        font_check();
        break;

    case 'k':
    case 'K':
    {
        /* Raw one-sector read, straight at the SDIO layer.  Separates "the
         * card/controller cannot read at all" from "FatFs cannot read". */
        static uint8_t sec[SDIO_BLOCK_SIZE];
        static const uint32_t blocks[] = { 0U, 4096U, 65536U };
        uint32_t i;

        for (i = 0U; i < sizeof(blocks) / sizeof(blocks[0]); i++)
        {
            HAL_StatusTypeDef st = bsp_sdio_read_blocks(sec, blocks[i], 1U);

            PRINT_LOG("[SDIO] block %lu -> %d  %02X %02X %02X %02X %02X %02X\r\n",
                      (unsigned long)blocks[i], (int)st,
                      sec[0], sec[1], sec[2], sec[3], sec[4], sec[5]);
        }
        break;
    }
#endif

    default:
        PRINT_LOG("[CMD ] unknown '%c' (h for help)\r\n", c);
        break;
    }
}
