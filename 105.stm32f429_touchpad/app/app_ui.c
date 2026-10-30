/**
 ******************************************************************************
 * @file    app_ui.c
 * @brief   Touchpad demo screen: an ink area plus a live gesture monitor.
 *
 *  Layout (480x800 portrait)
 *  -------------------------
 *      +----------------------------------+
 *      | title                      SD/FPS |  header  (48 px)
 *      +----------------------------------+
 *      |                                  |
 *      |         drawing area             |  canvas  (456x400)
 *      |         (strokes)                |
 *      |                                  |
 *      +----------------------------------+
 *      | gesture name                     |
 *      | counters / brush colour & width  |  monitor (456x220)
 *      | key hints                        |
 *      +----------------------------------+
 *      | [clear] [colour] [width] [paper] |  buttons (108x44)
 *      +----------------------------------+
 *      | storage / font / gesture hints   |  footer  (2 lines)
 *      +----------------------------------+
 *
 *  Everything here runs in the UI task (the only task allowed to touch LVGL).
 *  Strokes arrive as gesture events queued by the touch task, so no LVGL call
 *  is ever made from the input side.
 *
 *  Fonts: the CJK glyphs come from 0:/SYSTEM/FONT/GBKxx.FON (see
 *  bsp/lv_font_gbk.c); ASCII is served from the tables compiled into flash.
 *  If the card is missing every Chinese label is simply blank - the layout and
 *  the counters keep working.
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "lv_font_gbk.h"

#include "FreeRTOS.h"

#include "app_ui.h"
#include "gesture.h"
#include "image_sd.h"
#include "sd_card.h"
#include "bsp_key.h"
#include "bsp_lcd.h"
#include "bsp_lcd_text.h"
#include "bsp_touch.h"
#include "log.h"

/* -------------------------------------------------------------------------- */
/* Geometry                                                                   */
/* -------------------------------------------------------------------------- */
/*  The NT35510 on this board comes up with a 480x800 (portrait) GRAM window:
 *  see DFT_SCAN_DIR / lcd_scan_dir() in bsp_lcd.c and the boot log line
 *  "[LCD ] active GRAM window 480x800 (panel spec 800x480)".  102 derives its
 *  whole layout from that value; this file used to hardcode 800x480 landscape
 *  and therefore drew its right-hand 320 px straight off the panel.
 *
 *  Everything below is expressed for 480x800.  app_ui_create() logs a warning
 *  if the live window ever disagrees with SCR_W / SCR_H. */
#define SCR_W           480
#define SCR_H           800

#define HDR_H           48

/* Drawing area: full width, upper part. */
#define CV_X            12
#define CV_Y            56
#define CV_W            (SCR_W - 2 * CV_X)        /* 456 */
#define CV_H            400

/* Gesture monitor: full width, right below the drawing area. */
#define PN_X            12
#define PN_Y            468
#define PN_W            (SCR_W - 2 * PN_X)        /* 456 */
#define PN_H            220

/* Button row: four buttons across the full width. */
#define BTN_Y           700
#define BTN_H           44
#define BTN_GAP         8
#define BTN_W           ((SCR_W - 2 * CV_X - 3 * BTN_GAP) / 4)   /* 108 */

/* Footer: two lines of 12 px text. */
#define FTR_Y           748

/* -------------------------------------------------------------------------- */
/* Palette                                                                    */
/* -------------------------------------------------------------------------- */
#define C_BG            0x0E1116
#define C_CARD          0x141A22
#define C_BORDER        0x2A3340
#define C_TEXT          0xE6EDF3
#define C_DIM           0x8B949E
#define C_ACCENT        0x4C9AFF

/* Ink colours offered by KEY1 / the "colour" button. */
static const uint32_t k_brush_colors[] =
{
    0xE6EDF3,   /* white  */
    0xFF5A5A,   /* red    */
    0x4CD97B,   /* green  */
    0x4C9AFF,   /* blue   */
    0xFFC94C,   /* amber  */
    0xD96BFF,   /* violet */
};
#define BRUSH_COLOR_COUNT   (sizeof(k_brush_colors) / sizeof(k_brush_colors[0]))

/* Line widths offered by KEY2 / the "width" button. */
static const uint8_t k_brush_widths[] = { 2U, 4U, 8U, 12U };
#define BRUSH_WIDTH_COUNT   (sizeof(k_brush_widths) / sizeof(k_brush_widths[0]))

/* -------------------------------------------------------------------------- */
/* Strokes                                                                    */
/* -------------------------------------------------------------------------- */
#define STROKE_MAX       24
#define STROKE_PTS       256
#define STROKE_MIN_STEP  2      /* px between two recorded points */

typedef struct
{
    lv_obj_t   *line;
    lv_point_t *pts;
    uint16_t    n;
} stroke_t;

static stroke_t s_strokes[STROKE_MAX];
static uint16_t s_stroke_next = 0U;
static int      s_active      = -1;     /* index of the stroke being drawn */

/* -------------------------------------------------------------------------- */
/* Widgets                                                                    */
/* -------------------------------------------------------------------------- */
static lv_obj_t *s_scr;
static lv_obj_t *s_canvas;
static lv_obj_t *s_wallpaper;
static lv_obj_t *s_gesture_label;
static lv_obj_t *s_stat_pts;
static lv_obj_t *s_stat_taps;
static lv_obj_t *s_stat_moves;
static lv_obj_t *s_brush_label;
static lv_obj_t *s_swatch;
static lv_obj_t *s_sd_label;
static lv_obj_t *s_rate_label;

static uint8_t  s_color_idx = 0U;
static uint8_t  s_width_idx = 1U;

static uint32_t s_loop_count = 0U;
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
/* Ink                                                                        */
/* -------------------------------------------------------------------------- */
static void apply_brush(lv_obj_t *line)
{
    lv_obj_set_style_line_width(line, k_brush_widths[s_width_idx], 0);
    lv_obj_set_style_line_color(line, lv_color_hex(k_brush_colors[s_color_idx]), 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(line, 0, 0);
}

static void stroke_release_slot(uint16_t slot)
{
    stroke_t *st = &s_strokes[slot];

    if (st->line != NULL)
    {
        lv_obj_del(st->line);
        st->line = NULL;
    }
    if (st->pts != NULL)
    {
        vPortFree(st->pts);
        st->pts = NULL;
    }
    st->n = 0U;
}

/** Forget every stroke without touching LVGL: used when the screen is rebuilt
 *  and lv_obj_clean() has already destroyed the lv_line objects. */
static void strokes_forget(void)
{
    uint16_t i;

    for (i = 0U; i < STROKE_MAX; i++)
    {
        if (s_strokes[i].pts != NULL)
        {
            vPortFree(s_strokes[i].pts);
        }
        memset(&s_strokes[i], 0, sizeof(s_strokes[i]));
    }
    s_stroke_next = 0U;
    s_active      = -1;
}

static void strokes_clear(void)
{
    uint16_t i;

    for (i = 0U; i < STROKE_MAX; i++)
    {
        stroke_release_slot(i);
    }
    s_stroke_next = 0U;
    s_active      = -1;
    lv_label_set_text(s_gesture_label, "画板已清空");
}

/** @retval 1 when (x,y) is inside the drawing area, in canvas coordinates. */
static int canvas_from_screen(int32_t ax, int32_t ay, int32_t *cx, int32_t *cy)
{
    int32_t x = ax - CV_X;
    int32_t y = ay - CV_Y;

    if ((x < 0) || (y < 0) || (x >= CV_W) || (y >= CV_H))
    {
        return 0;
    }
    *cx = x;
    *cy = y;
    return 1;
}

static void stroke_begin(int32_t ax, int32_t ay)
{
    int32_t x;
    int32_t y;
    uint16_t slot;
    stroke_t *st;

    if (canvas_from_screen(ax, ay, &x, &y) == 0)
    {
        return;
    }

    slot = s_stroke_next;
    s_stroke_next = (uint16_t)((s_stroke_next + 1U) % STROKE_MAX);

    stroke_release_slot(slot);
    st = &s_strokes[slot];

    st->pts = (lv_point_t *)pvPortMalloc(sizeof(lv_point_t) * STROKE_PTS);
    if (st->pts == NULL)
    {
        PRINT_LOG("[UI  ] stroke buffer alloc failed\r\n");
        return;
    }

    st->line = lv_line_create(s_canvas);
    apply_brush(st->line);

    st->pts[0].x = (lv_coord_t)x;
    st->pts[0].y = (lv_coord_t)y;
    st->n        = 1U;
    lv_line_set_points(st->line, st->pts, st->n);

    s_active = (int)slot;
}

static void stroke_add(int32_t ax, int32_t ay)
{
    stroke_t *st;
    int32_t x;
    int32_t y;
    int32_t dx;
    int32_t dy;

    if (s_active < 0)
    {
        return;
    }
    st = &s_strokes[s_active];

    /* Clamp instead of dropping: a finger sliding out of the area must keep
     * extending the stroke along the border, not break it. */
    x = ax - CV_X;
    y = ay - CV_Y;
    if (x < 0)      { x = 0; }
    if (y < 0)      { y = 0; }
    if (x >= CV_W)  { x = CV_W - 1; }
    if (y >= CV_H)  { y = CV_H - 1; }

    if ((st->pts == NULL) || (st->n == 0U) || (st->n >= STROKE_PTS))
    {
        return;
    }

    dx = x - st->pts[st->n - 1U].x;
    dy = y - st->pts[st->n - 1U].y;
    if (((dx < 0) ? -dx : dx) + ((dy < 0) ? -dy : dy) < STROKE_MIN_STEP)
    {
        return;
    }

    st->pts[st->n].x = (lv_coord_t)x;
    st->pts[st->n].y = (lv_coord_t)y;
    st->n++;
    lv_line_set_points(st->line, st->pts, st->n);
}

static void stroke_end(void)
{
    s_active = -1;
}

/** A tap stamps a round dot: two points 1 px apart + round caps. */
static void stamp_dot(int32_t ax, int32_t ay)
{
    int32_t x;
    int32_t y;

    if (canvas_from_screen(ax, ay, &x, &y) == 0)
    {
        return;
    }

    stroke_begin(ax, ay);
    if (s_active >= 0)
    {
        stroke_add(ax + 1, ay);
        stroke_end();
    }
}

/* -------------------------------------------------------------------------- */
/* Gesture monitor                                                            */
/* -------------------------------------------------------------------------- */
static void set_gesture_text(const char *txt)
{
    lv_label_set_text(s_gesture_label, txt);
}

static void handle_event(const gesture_evt_t *ev)
{
    switch (ev->type)
    {
    case GESTURE_EVT_PRESS:
        stroke_begin(ev->x, ev->y);
        set_gesture_text("单指拖拽");
        break;

    case GESTURE_EVT_MOVE:
        stroke_add(ev->x, ev->y);
        break;

    case GESTURE_EVT_RELEASE:
        stroke_end();
        break;

    case GESTURE_EVT_TAP:
        stroke_end();
        stamp_dot(ev->x, ev->y);
        set_gesture_text("单指单击");
        break;

    case GESTURE_EVT_TAP2:
        stroke_end();
        set_gesture_text("双指单击");
        break;

    case GESTURE_EVT_TAP3:
        stroke_end();
        set_gesture_text("三指单击");
        break;

    case GESTURE_EVT_SCROLL:
        set_gesture_text((ev->d >= 0) ? "双指下滑 ↓" : "双指上滑 ↑");
        break;

    case GESTURE_EVT_PAN:
        set_gesture_text((ev->d >= 0) ? "双指右滑 →" : "双指左滑 ←");
        break;

    default:
        break;
    }
}

static void refresh_stats(void)
{
    gesture_status_t st;

    gesture_get_status(&st);

    lv_label_set_text_fmt(s_stat_pts, "触点 %u   笔画 %lu",
                          (unsigned int)st.fingers, (unsigned long)st.presses);
    lv_label_set_text_fmt(s_stat_taps, "单指 %lu   多指 %lu",
                          (unsigned long)st.taps, (unsigned long)st.multi_taps);
    lv_label_set_text_fmt(s_stat_moves, "滚动 %lu   平移 %lu",
                          (unsigned long)st.scrolls, (unsigned long)st.pans);

    lv_label_set_text_fmt(s_brush_label, "颜色 %u/%u   粗细 %u px",
                          (unsigned int)(s_color_idx + 1U), (unsigned int)BRUSH_COLOR_COUNT,
                          (unsigned int)k_brush_widths[s_width_idx]);
    lv_obj_set_style_bg_color(s_swatch, lv_color_hex(k_brush_colors[s_color_idx]), 0);
}

/* -------------------------------------------------------------------------- */
/* Actions                                                                    */
/* -------------------------------------------------------------------------- */
static void action_color(void)
{
    s_color_idx = (uint8_t)((s_color_idx + 1U) % BRUSH_COLOR_COUNT);
    refresh_stats();
    set_gesture_text("画笔颜色已切换");
}

static void action_width(void)
{
    s_width_idx = (uint8_t)((s_width_idx + 1U) % BRUSH_WIDTH_COUNT);
    refresh_stats();
    set_gesture_text("画笔粗细已切换");
}

static void action_wallpaper(void)
{
    if (s_wallpaper == NULL)
    {
        s_wallpaper = image_sd_bg(s_scr);
    }

    if (s_wallpaper == NULL)
    {
        set_gesture_text("无壁纸（SD 0:/IMG/bg.bin）");
        return;
    }

    if (lv_obj_has_flag(s_wallpaper, LV_OBJ_FLAG_HIDDEN))
    {
        lv_obj_clear_flag(s_wallpaper, LV_OBJ_FLAG_HIDDEN);
        set_gesture_text("壁纸已显示");
    }
    else
    {
        lv_obj_add_flag(s_wallpaper, LV_OBJ_FLAG_HIDDEN);
        set_gesture_text("壁纸已隐藏");
    }
}

static void cb_clear(lv_event_t *e)      { (void)e; strokes_clear(); }
static void cb_colour(lv_event_t *e)     { (void)e; action_color(); }
static void cb_width(lv_event_t *e)      { (void)e; action_width(); }
static void cb_wallpaper(lv_event_t *e)  { (void)e; action_wallpaper(); }

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

    label = lv_label_create(s_scr);
    lv_label_set_text(label, ascii_msg);
    lv_obj_set_style_text_font(label, &lv_font_gbk_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(C_DIM), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -12);

    label = lv_label_create(s_scr);
    lv_label_set_text(label, "STM32F429  LVGL 8.3  SD storage");
    lv_obj_set_style_text_font(label, &lv_font_gbk_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(C_BORDER), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 16);
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *txt, lv_coord_t x,
                             lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_t *label;

    lv_obj_set_pos(btn, x, BTN_Y);
    lv_obj_set_size(btn, BTN_W, BTN_H);
    lv_obj_set_style_bg_color(btn, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(C_BORDER), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    label = make_label(btn, &lv_font_gbk_16, C_TEXT);
    lv_label_set_text(label, txt);
    lv_obj_center(label);
    return btn;
}

void app_ui_create(void)
{
    lv_obj_t *card;
    lv_obj_t *label;
    uint32_t total_mb = 0U;
    uint32_t used_mb  = 0U;

    s_scr = lv_scr_act();
    lv_obj_clean(s_scr);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(C_BG), 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* The constants above assume the 480x800 portrait window this panel
     * reports.  Say so loudly if that ever changes, instead of silently
     * drawing the layout half off-screen. */
    if ((LCD_Width != SCR_W) || (LCD_Height != SCR_H))
    {
        PRINT_LOG("[UI  ] WARN layout is %dx%d but the panel is %lux%lu\r\n",
                  SCR_W, SCR_H, (unsigned long)LCD_Width, (unsigned long)LCD_Height);
    }

    /* Screen is rebuilt when the card shows up later: the widgets are gone but
     * the point buffers are ours, so hand them back and drop the pointers. */
    strokes_forget();
    s_wallpaper = NULL;

    /* ---- Wallpaper (from the card, optional) ---------------------------- */
    s_wallpaper = image_sd_bg(s_scr);

    /* ---- Header ---------------------------------------------------------- */
    label = make_label(s_scr, &lv_font_gbk_24, C_TEXT);
    lv_label_set_text(label, "触摸板  TOUCHPAD");
    lv_obj_set_pos(label, 16, 8);

    s_sd_label = make_label(s_scr, &lv_font_gbk_12, C_DIM);
    lv_obj_align(s_sd_label, LV_ALIGN_TOP_RIGHT, -16, 10);

    s_rate_label = make_label(s_scr, &lv_font_gbk_12, C_DIM);
    lv_obj_align(s_rate_label, LV_ALIGN_TOP_RIGHT, -16, 28);

    /* ---- Drawing area ---------------------------------------------------- */
    s_canvas = make_card(s_scr, CV_X, CV_Y, CV_W, CV_H);
    lv_obj_set_style_bg_color(s_canvas, lv_color_hex(0x11161D), 0);
    lv_obj_set_style_pad_all(s_canvas, 0, 0);
    lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);

    /* ---- Gesture monitor ------------------------------------------------- */
    card = make_card(s_scr, PN_X, PN_Y, PN_W, PN_H);

    label = make_label(card, &lv_font_gbk_16, C_DIM);
    lv_label_set_text(label, "手势识别");
    lv_obj_set_pos(label, 0, 0);

    s_gesture_label = make_label(card, &lv_font_gbk_24, C_ACCENT);
    lv_label_set_text(s_gesture_label, "等待触摸…");
    lv_obj_set_pos(s_gesture_label, 0, 24);
    lv_obj_set_width(s_gesture_label, PN_W - 16);

    s_stat_pts = make_label(card, &lv_font_gbk_16, C_TEXT);
    lv_obj_set_pos(s_stat_pts, 0, 58);

    s_stat_taps = make_label(card, &lv_font_gbk_16, C_TEXT);
    lv_obj_set_pos(s_stat_taps, 0, 82);

    s_stat_moves = make_label(card, &lv_font_gbk_16, C_TEXT);
    lv_obj_set_pos(s_stat_moves, 0, 106);

    label = make_label(card, &lv_font_gbk_16, C_DIM);
    lv_label_set_text(label, "画笔");
    lv_obj_set_pos(label, 0, 132);

    s_swatch = lv_obj_create(card);
    lv_obj_set_pos(s_swatch, 0, 156);
    lv_obj_set_size(s_swatch, 40, 24);
    lv_obj_set_style_radius(s_swatch, 6, 0);
    lv_obj_set_style_border_color(s_swatch, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_border_width(s_swatch, 1, 0);
    lv_obj_clear_flag(s_swatch, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_brush_label = make_label(card, &lv_font_gbk_16, C_TEXT);
    lv_obj_set_pos(s_brush_label, 52, 158);

    label = make_label(card, &lv_font_gbk_12, C_DIM);
    lv_label_set_text(label, "KEY0 清屏 KEY1 色 KEY2 粗细 KEY_UP 壁纸");
    lv_obj_set_pos(label, 0, 186);

    /* ---- Buttons --------------------------------------------------------- */
    (void)make_button(s_scr, "清屏",   CV_X, cb_clear);
    (void)make_button(s_scr, "换色",   CV_X + (BTN_W + BTN_GAP), cb_colour);
    (void)make_button(s_scr, "粗细",   CV_X + 2 * (BTN_W + BTN_GAP), cb_width);
    (void)make_button(s_scr, "壁纸",   CV_X + 3 * (BTN_W + BTN_GAP), cb_wallpaper);

    /* ---- Footer ---------------------------------------------------------- */
    if (sd_card_is_ready() != 0)
    {
        if (sd_card_usage_mb(&total_mb, &used_mb) == RT_OK)
        {
            lv_label_set_text_fmt(s_sd_label, "SD 0: %lu MB / %lu MB 起",
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
    lv_label_set_text(label, "字库 0:/SYSTEM/FONT  图片 0:/IMG\n"
                             "手势 双击/双指滚动/双指平移");
    lv_obj_set_pos(label, 12, FTR_Y);

    refresh_stats();
    set_gesture_text("等待触摸…");
}

/* -------------------------------------------------------------------------- */
/* Poll                                                                       */
/* -------------------------------------------------------------------------- */
static void poll_keys(void)
{
    uint32_t now = HAL_GetTick();

    if ((now - s_key_mark) < BSP_KEY_SCAN_PERIOD_MS)
    {
        return;
    }
    s_key_mark = now;

    bsp_key_poll();

    if (bsp_key_edge(BSP_KEY_0) != 0U)
    {
        strokes_clear();
    }
    if (bsp_key_edge(BSP_KEY_1) != 0U)
    {
        action_color();
    }
    if (bsp_key_edge(BSP_KEY_2) != 0U)
    {
        action_width();
    }
    if (bsp_key_edge(BSP_KEY_UP) != 0U)
    {
        action_wallpaper();
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

    poll_keys();

    now = HAL_GetTick();
    s_loop_count++;

    if ((now - s_rate_mark) >= 1000U)
    {
        uint32_t rate = s_loop_count * 1000U / (now - s_rate_mark);

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
 *  stays on the task that owns the display.  Responses go through PRINT_LOG so
 *  the global PRINT_LOG_ENABLE switch still controls the binary size.        */

#if PRINT_LOG_ENABLE
static void report_status(void)
{
    gesture_status_t st;
    uint32_t total_mb = 0U;
    uint32_t used_mb  = 0U;

    gesture_get_status(&st);
    (void)sd_card_usage_mb(&total_mb, &used_mb);

    PRINT_LOG("[SD  ] %s %lu/%lu MB\r\n",
              (sd_card_is_ready() != 0) ? "0: mounted" : "0: absent",
              (unsigned long)used_mb, (unsigned long)total_mb);
    PRINT_LOG("[FONT] src=%s mask=0x%02lX\r\n",
              lcd_driver_font_source(),
              (unsigned long)lcd_driver_font_status());
    PRINT_LOG("[TOUCH] irq=%lu ready=%d\r\n",
              (unsigned long)bsp_touch_irq_count(), bsp_touch_is_ready());
    PRINT_LOG("[GEST] fingers=%u press=%lu tap=%lu multi=%lu scroll=%lu pan=%lu drop=%lu\r\n",
              (unsigned int)st.fingers,
              (unsigned long)st.presses, (unsigned long)st.taps,
              (unsigned long)st.multi_taps, (unsigned long)st.scrolls,
              (unsigned long)st.pans, (unsigned long)st.dropped);
    PRINT_LOG("[BRUSH] color=%u/%u width=%u px\r\n",
              (unsigned int)(s_color_idx + 1U), (unsigned int)BRUSH_COLOR_COUNT,
              (unsigned int)k_brush_widths[s_width_idx]);
    PRINT_LOG("[UI  ] screen=%d gesture_label=%d\r\n",
              (s_scr != NULL) ? 1 : 0, (s_gesture_label != NULL) ? 1 : 0);
}

static void report_help(void)
{
    PRINT_LOG("[CMD ] c=clear k=colour w=width b=wallpaper s=status h=help\r\n");
}
#endif /* PRINT_LOG_ENABLE */

void app_ui_command(char c)
{
    if ((s_scr == NULL) || (s_gesture_label == NULL))
    {
        PRINT_LOG("[CMD ] screen not ready\r\n");
        return;
    }

    switch (c)
    {
    case 'c':
    case 'C':
        strokes_clear();
        set_gesture_text("串口: 清屏");
        PRINT_LOG("[CMD ] clear\r\n");
        break;

    case 'k':
    case 'K':
        action_color();
        PRINT_LOG("[CMD ] colour -> %u/%u\r\n",
                  (unsigned int)(s_color_idx + 1U), (unsigned int)BRUSH_COLOR_COUNT);
        break;

    case 'w':
    case 'W':
        action_width();
        PRINT_LOG("[CMD ] width -> %u px\r\n", (unsigned int)k_brush_widths[s_width_idx]);
        break;

    case 'b':
    case 'B':
        action_wallpaper();
        PRINT_LOG("[CMD ] wallpaper toggle\r\n");
        break;

#if PRINT_LOG_ENABLE
    case 's':
    case 'S':
    case '?':
        report_status();
        break;

    case 'h':
    case 'H':
        report_help();
        break;
#endif

    default:
        PRINT_LOG("[CMD ] unknown '%c' (h for help)\r\n", c);
        break;
    }
}
