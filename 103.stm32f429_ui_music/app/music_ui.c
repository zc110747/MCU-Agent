/**
  ******************************************************************************
  * @file    app/music_ui.c
  * @brief   Screen 2 - music player UI.
  *
  *  Layout (proportional to the live resolution, so it adapts to either
  *  orientation the LCD driver reports):
  *    - top:    song title (centre) + "index / count"
  *    - mid:    draggable progress track with current / total time
  *    - lower:  [上一首] [播放/暂停] [下一首] transport row (centre)
  *    - right:  vertical volume bar + [+] / [-] buttons + volume number
  *
  *  lv_slider is disabled in lv_conf (only lv_bar is on), so the progress bar
  *  is a clickable track whose PRESSING/CLICKED event seeks by pointer x; the
  *  fill and handle are plain objects resized each refresh.
  ******************************************************************************
  */
#include "music_ui.h"
#include "audio_player.h"
#include "ui_common.h"
#include "log.h"

#include <stdio.h>
#include <string.h>

/* ---- Widget handles ------------------------------------------------------- */
static lv_obj_t *s_title;
static lv_obj_t *s_idx;
static lv_obj_t *s_track;
static lv_obj_t *s_fill;
static lv_obj_t *s_handle;
static lv_obj_t *s_time_cur;
static lv_obj_t *s_time_tot;
static lv_obj_t *s_play_lbl;
static lv_obj_t *s_vol_bar;
static lv_obj_t *s_vol_fill;
static lv_obj_t *s_vol_num;
static lv_timer_t *s_refresh;
static lv_coord_t m_w;   /* live screen width  (local, not ui_common s_w) */
static lv_coord_t m_h;   /* live screen height (local, not ui_common s_h) */

/* ---- Forward declarations ------------------------------------------------- */
static void btn_cb(lv_event_t *e);
static void progress_cb(lv_event_t *e);

/* ---- Small helpers -------------------------------------------------------- */
static lv_obj_t *mk_text(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                         const lv_font_t *font, uint32_t color,
                         const char *text, lv_coord_t w)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(lbl, 0, LV_PART_MAIN);
    lv_label_set_text(lbl, text);
    lv_obj_set_width(lbl, w);
    lv_obj_set_pos(lbl, x, y);
    return lbl;
}

static lv_obj_t *mk_btn(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
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
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_event_cb(btn, btn_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)code);
    return btn;
}

/* ---- Callbacks ------------------------------------------------------------ */
static void btn_cb(lv_event_t *e)
{
    int code = (int)(intptr_t)lv_event_get_user_data(e);

    if (code == 0)
    {
        player_prev();
    }
    else if (code == 1)
    {
        player_toggle();
    }
    else if (code == 2)
    {
        player_next();
    }
    else if (code == 10)   /* volume - */
    {
        int v = (int)player_get_volume() - 5;
        if (v < 0) { v = 0; }
        player_set_volume((uint8_t)v);
    }
    else if (code == 11)   /* volume + */
    {
        int v = (int)player_get_volume() + 5;
        if (v > 100) { v = 100; }
        player_set_volume((uint8_t)v);
    }
    (void)e;
}

static void update_progress_visual(uint32_t pct)
{
    lv_coord_t tw = lv_obj_get_width(s_track);
    lv_coord_t w  = (lv_coord_t)((pct * (uint32_t)tw) / 100U);
    if (w < 2) { w = 2; }

    lv_obj_set_width(s_fill, w);
    lv_obj_set_pos(s_handle, w - 5, -4);
}

static void progress_cb(lv_event_t *e)
{
    lv_point_t p;
    lv_coord_t x;
    lv_coord_t tw;
    uint32_t pct;

    lv_indev_get_point(lv_indev_get_act(), &p);
    tw = lv_obj_get_width(s_track);
    x  = p.x - lv_obj_get_x(s_track);
    if (x < 0) { x = 0; }
    if (x > tw) { x = tw; }
    pct = (uint32_t)((x * 100) / tw);

    player_seek_percent(pct);
    update_progress_visual(pct);
}

static void music_refresh_cb(lv_timer_t *t)
{
    uint32_t dur = player_duration_ms();
    uint32_t pos = player_position_ms();
    uint32_t pct = (dur > 0U) ? (pos * 100U) / dur : 0U;
    uint32_t cnt = player_count();
    uint8_t  v   = player_get_volume();
    lv_coord_t barh = lv_obj_get_height(s_vol_bar);
    lv_coord_t fh   = (lv_coord_t)((v * barh) / 100U);

    LV_UNUSED(t);

    if (s_title != NULL)
    {
        lv_label_set_text(s_title,
                          (cnt == 0U) ? "未找到音乐文件" : player_current_title());
    }
    if (s_idx != NULL)
    {
        lv_label_set_text_fmt(s_idx, "%d / %lu",
                              player_current_index() + 1, (unsigned long)cnt);
    }
    update_progress_visual(pct);
    if (s_time_cur != NULL)
    {
        lv_label_set_text_fmt(s_time_cur, "%02d:%02d",
                              (int)(pos / 60000U), (int)((pos / 1000U) % 60U));
    }
    if (s_time_tot != NULL)
    {
        lv_label_set_text_fmt(s_time_tot, "%02d:%02d",
                              (int)(dur / 60000U), (int)((dur / 1000U) % 60U));
    }
    if (s_play_lbl != NULL)
    {
        lv_label_set_text(s_play_lbl,
                          (player_state() == PLAYER_PLAYING) ? "暂停" : "播放");
    }
    if (s_vol_num != NULL)
    {
        lv_label_set_text_fmt(s_vol_num, "%u", (unsigned)v);
    }
    if (s_vol_fill != NULL)
    {
        lv_obj_set_height(s_vol_fill, fh);
        lv_obj_align_to(s_vol_fill, s_vol_bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
}

/* ---- Build ---------------------------------------------------------------- */
void music_ui_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_coord_t cx;
    lv_coord_t tw;
    lv_coord_t track_y;
    lv_coord_t row_y;
    lv_coord_t btn_w, btn_h, gap, start_x, total_w;
    lv_coord_t vpx, vpy, vph, vbtn, barw, barh, barx, bary;

    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr, 0, LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    m_w = (lv_coord_t)lv_disp_get_hor_res(NULL);
    m_h = (lv_coord_t)lv_disp_get_ver_res(NULL);
    if (m_w <= 0) { m_w = 1; }
    if (m_h <= 0) { m_h = 1; }
    cx = m_w / 2;

    /* Title + index. */
    s_title = mk_text(scr, 0, 14, &lv_font_gbk_24, COL_TXT, "音乐播放器", m_w);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    s_idx = mk_text(scr, 0, 46, &lv_font_gbk_16, COL_DIM, "0 / 0", m_w);
    lv_obj_set_style_text_align(s_idx, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    /* Progress track. */
    tw = m_w - (2 * UI_PAD);
    track_y = (lv_coord_t)(m_h * 0.30f);
    s_track = lv_obj_create(scr);
    lv_obj_remove_style_all(s_track);
    lv_obj_set_size(s_track, tw, 14);
    lv_obj_set_pos(s_track, UI_PAD, track_y);
    lv_obj_set_style_bg_color(s_track, lv_color_hex(COL_HDR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_track, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_track, 4, LV_PART_MAIN);
    lv_obj_clear_flag(s_track, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_track, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_track, progress_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_track, progress_cb, LV_EVENT_CLICKED, NULL);

    s_fill = lv_obj_create(s_track);
    lv_obj_remove_style_all(s_fill);
    lv_obj_set_size(s_fill, 2, 14);
    lv_obj_set_pos(s_fill, 0, 0);
    lv_obj_set_style_bg_color(s_fill, lv_color_hex(COL_TXT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_fill, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_fill, LV_OBJ_FLAG_CLICKABLE);

    s_handle = lv_obj_create(s_track);
    lv_obj_remove_style_all(s_handle);
    lv_obj_set_size(s_handle, 10, 22);
    lv_obj_set_pos(s_handle, -3, -4);
    lv_obj_set_style_bg_color(s_handle, lv_color_hex(COL_DIM), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_handle, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_handle, LV_OBJ_FLAG_CLICKABLE);

    s_time_cur = mk_text(scr, UI_PAD, (lv_coord_t)(track_y - 22),
                         &lv_font_gbk_16, COL_TXT, "00:00", 80);
    s_time_tot = mk_text(scr, (lv_coord_t)(m_w - UI_PAD - 80),
                         (lv_coord_t)(track_y - 22), &lv_font_gbk_16,
                         COL_TXT, "00:00", 80);
    lv_obj_set_style_text_align(s_time_tot, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

    /* Transport row. */
    btn_w = 92; btn_h = 64; gap = 24;
    total_w = 3 * btn_w + 2 * gap;
    start_x = cx - (total_w / 2);
    row_y = (lv_coord_t)(m_h * 0.62f);
    (void)mk_btn(scr, start_x, row_y, btn_w, btn_h, "上一首", 0);
    {
        lv_obj_t *play_btn = mk_btn(scr, (lv_coord_t)(start_x + btn_w + gap),
                                    row_y, btn_w, btn_h, "播放", 1);
        s_play_lbl = lv_obj_get_child(play_btn, 0); /* label inside the button */
    }
    (void)mk_btn(scr, (lv_coord_t)(start_x + 2 * (btn_w + gap)), row_y,
                 btn_w, btn_h, "下一首", 2);

    /* Volume panel (right). */
    vpx = (lv_coord_t)(m_w - UI_PAD - 96);
    vpy = (lv_coord_t)(m_h * 0.28f);
    vph = (lv_coord_t)(m_h * 0.52f);
    vbtn = 44;
    barw = 24;
    barh = (lv_coord_t)(vph - (2 * vbtn) - 24);
    barx = (lv_coord_t)(vpx + 36);
    bary = (lv_coord_t)(vpy + vbtn + 12);

    (void)mk_text(scr, vpx, (lv_coord_t)(vpy - 26), &lv_font_gbk_16,
                  COL_DIM, "音量", 80);
    (void)mk_btn(scr, vpx, vpy, 96, vbtn, "+", 11);
    (void)mk_btn(scr, vpx, (lv_coord_t)(vpy + vph - vbtn), 96, vbtn, "-", 10);

    s_vol_bar = lv_obj_create(scr);
    lv_obj_remove_style_all(s_vol_bar);
    lv_obj_set_size(s_vol_bar, barw, barh);
    lv_obj_set_pos(s_vol_bar, barx, bary);
    lv_obj_set_style_bg_color(s_vol_bar, lv_color_hex(COL_HDR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_vol_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_vol_bar, lv_color_hex(COL_DIM), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_vol_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_vol_bar, 4, LV_PART_MAIN);
    lv_obj_clear_flag(s_vol_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_vol_fill = lv_obj_create(s_vol_bar);
    lv_obj_remove_style_all(s_vol_fill);
    lv_obj_set_size(s_vol_fill, barw, 0);
    lv_obj_set_style_bg_color(s_vol_fill, lv_color_hex(COL_TXT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_vol_fill, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_vol_fill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align_to(s_vol_fill, s_vol_bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    s_vol_num = mk_text(scr, barx, (lv_coord_t)(bary + barh + 6),
                        &lv_font_gbk_24, COL_TXT, "70", 60);
    lv_obj_set_style_text_align(s_vol_num, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    /* 10 Hz refresh for smooth progress + live state. */
    if (s_refresh != NULL)
    {
        lv_timer_del(s_refresh);
    }
    s_refresh = lv_timer_create(music_refresh_cb, 100, NULL);
}
