/**
  ******************************************************************************
  * @file    app/ui/page_rtc.c
  * @brief   Page 3 - RTC clock + time / alarm setting.
  *
  *  Large centered clock (year-month-day hour:minute:second) from the internal
  *  RTC, plus a 时间设置 band (year/month/day/hour/minute select + up/down +
  *  设置更新时间) and an 闹钟 band (hour/minute select + 上/下/设置 + 闹钟开启/
  *  闹钟关闭).  The alarm is persisted to EEPROM (see bsp_rtc.c) and armed via
  *  the backup registers on power-up.
  *
  *  Interaction rules (see user request):
  *    - 上 / 下 wrap around (max -> min, min -> max) on both time and alarm.
  *    - The alarm status label (闹钟 HH:MM 开/关) is updated ONLY on a commit
  *      (设置 / 闹钟开启 / 闹钟关闭); editing with 上/下 does not touch it.
  *    - 闹钟关闭 also silences the buzzer.
  *    - After the alarm fires it rings for 60 s, then auto-disables (persists
  *      off, stops the buzzer, refreshes the label).
  ******************************************************************************
  */
#include "ui_common.h"

static void rtc_btn_cb(lv_event_t *e);
static void rtc_cell_cb(lv_event_t *e);

/* ---- Time edit group (year/month/day/hour/min) ---- */
#define RTC_TFIELD_COUNT 5
static const char *s_tname[RTC_TFIELD_COUNT] = {"年", "月", "日", "时", "分"};
static int  s_edit[RTC_TFIELD_COUNT];     /* yr, mon, day, hh, mm (edit buffer) */
static int  s_tfield = 3;                 /* selected time field (default 时) */

/* ---- Alarm edit group (hour/min) ---- */
#define RTC_AFIELD_COUNT 2
static const char *s_aname[RTC_AFIELD_COUNT] = {"时", "分"};
static int  s_alarm[RTC_AFIELD_COUNT] = {0, 0};   /* hh, mm */
static int  s_afield = 0;                 /* selected alarm field (default 时) */
static uint8_t s_alarm_on = 0U;           /* alarm enabled flag */

/* 0 = editing time, 1 = editing alarm.  Up/Down act on the active group. */
static int  s_selgrp = 0;

/* Alarm ring state (software compare in the 2 Hz UI tick). */
static uint8_t  s_alarm_beeping     = 0U;  /* buzzer currently driven on        */
static uint8_t  s_alarm_ringing      = 0U;  /* 1 while the 60 s alarm session runs */
static uint16_t s_alarm_ring_ticks   = 0U;  /* 2 Hz ticks since the trigger       */
static int      s_alarm_rung_min     = -1;  /* minute already rung this enable   */

/* Clock display handles (two big centered lines). */
static lv_obj_t *p3_date = NULL;          /* clock line 1: 年-月-日 */
static lv_obj_t *p3_time = NULL;          /* clock line 2: 时:分:秒 */
static lv_obj_t *p3_tcell[RTC_TFIELD_COUNT];
static lv_obj_t *p3_tval[RTC_TFIELD_COUNT];
static lv_obj_t *p3_acell[RTC_AFIELD_COUNT];
static lv_obj_t *p3_aval[RTC_AFIELD_COUNT];
static lv_obj_t *p3_alabel = NULL;        /* alarm status label */

/* Up / down chevrons (line glyphs, font-independent like the nav bar). */
static const lv_point_t s_arrow_up[3]   = { { 4, 16 }, { 10, 4 }, { 16, 16 } };
static const lv_point_t s_arrow_down[3] = { { 4, 4 }, { 10, 16 }, { 16, 4 } };

static int rtc_days_in_month(int yr, int mon)
{
    static const int dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if (mon < 1 || mon > 12) return 0;
    if (mon == 2 && ((yr % 4 == 0 && yr % 100 != 0) || yr % 400 == 0)) return 29;
    return dim[mon - 1];
}

static void rtc_load_edit(void)
{
    int yr, mon, day, hh, mm, ss;
    int ah = 0, am = 0, on = 0;

    BSP_RTC_Get(&yr, &mon, &day, &hh, &mm, &ss);
    s_edit[0] = yr;  s_edit[1] = mon; s_edit[2] = day;
    s_edit[3] = hh;  s_edit[4] = mm;

    s_tfield = 3;        /* default selection: 时 */

    BSP_RTC_Alarm_Get(&ah, &am, &on);
    s_alarm[0] = ah;  s_alarm[1] = am;
    s_alarm_on = (uint8_t)on;
    s_afield = 0;        /* default alarm selection: 时 */
    s_selgrp = 0;        /* start editing time */
}

void rtc_refresh_clock(void)
{
    int yr, mon, day, hh, mm, ss;
    char dbuf[16];
    char tbuf[16];

    BSP_RTC_Get(&yr, &mon, &day, &hh, &mm, &ss);
    (void)snprintf(dbuf, sizeof(dbuf), "%d-%d-%d", yr, mon, day);
    (void)snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d", hh, mm, ss);
    if (p3_date != NULL) lv_label_set_text(p3_date, dbuf);
    if (p3_time != NULL) lv_label_set_text(p3_time, tbuf);
}

static void rtc_refresh_fields(void)
{
    char buf[32];
    int  i;

    for (i = 0; i < RTC_TFIELD_COUNT; i++)
    {
        (void)snprintf(buf, sizeof(buf), "%d", s_edit[i]);
        if (p3_tval[i] != NULL) lv_label_set_text(p3_tval[i], buf);
        {
            int sel = (s_selgrp == 0 && i == s_tfield) ? 1 : 0;
            if (p3_tcell[i] != NULL)
            {
                lv_obj_set_style_border_color(p3_tcell[i],
                        lv_color_hex(sel ? COL_TXT : COL_DIM), LV_PART_MAIN);
                lv_obj_set_style_border_width(p3_tcell[i], sel ? 2 : 1, LV_PART_MAIN);
            }
        }
    }

    for (i = 0; i < RTC_AFIELD_COUNT; i++)
    {
        (void)snprintf(buf, sizeof(buf), "%02d", s_alarm[i]);
        if (p3_aval[i] != NULL) lv_label_set_text(p3_aval[i], buf);
        {
            int sel = (s_selgrp == 1 && i == s_afield) ? 1 : 0;
            if (p3_acell[i] != NULL)
            {
                lv_obj_set_style_border_color(p3_acell[i],
                        lv_color_hex(sel ? COL_TXT : COL_DIM), LV_PART_MAIN);
                lv_obj_set_style_border_width(p3_acell[i], sel ? 2 : 1, LV_PART_MAIN);
            }
        }
    }

    /* NOTE: the alarm status label is NOT touched here.  It is updated only by
     * rtc_refresh_alabel(), which runs on a commit (设置 / 开启 / 关闭). */
}

/* The alarm status label: shows the COMMITTED alarm (time + on/off).  Called
 * only from 设置 / 闹钟开启 / 闹钟关闭, never from 上/下 editing. */
static void rtc_refresh_alabel(void)
{
    if (p3_alabel != NULL)
    {
        char buf[32];
        (void)snprintf(buf, sizeof(buf), "闹钟 %02d:%02d %s",
                       s_alarm[0], s_alarm[1], s_alarm_on ? "开" : "关");
        lv_label_set_text(p3_alabel, buf);
    }
}

static void rtc_adj(int delta)
{
    int f, v, lo, hi;

    if (s_selgrp == 1)                  /* alarm group */
    {
        f = s_afield;
        lo = 0;  hi = (f == 0) ? 23 : 59;
        v = s_alarm[f] + delta;
        if (v < lo) v = hi;             /* wrap: below min -> max */
        else if (v > hi) v = lo;       /* wrap: above max -> min */
        s_alarm[f] = v;
        rtc_refresh_fields();
        return;
    }

    /* time group */
    f = s_tfield;
    if (f == 0)      { lo = 2000; hi = 2100; }
    else if (f == 1) { lo = 1;    hi = 12; }
    else if (f == 2) { lo = 1;    hi = rtc_days_in_month(s_edit[0], s_edit[1]); }
    else if (f == 3) { lo = 0;    hi = 23; }
    else             { lo = 0;    hi = 59; }

    v = s_edit[f] + delta;
    if (v < lo) v = hi;                 /* wrap around (up & down both cycle) */
    else if (v > hi) v = lo;
    s_edit[f] = v;

    /* Keep the day valid when the month or year changes. */
    if (f == 0 || f == 1)
    {
        int dmax = rtc_days_in_month(s_edit[0], s_edit[1]);
        if (s_edit[2] > dmax) s_edit[2] = dmax;
    }
    rtc_refresh_fields();
}

static void rtc_apply(void)
{
    if (s_selgrp == 1)                  /* alarm group: persist the alarm */
    {
        (void)BSP_RTC_Alarm_Set(s_alarm[0], s_alarm[1], (int)s_alarm_on);
        (void)BSP_RTC_Alarm_Persist(s_alarm[0], s_alarm[1], (int)s_alarm_on);
        PRINT_LOG("[UI  ] rtc apply alarm %02d:%02d %s\r\n",
                  s_alarm[0], s_alarm[1], s_alarm_on ? "on" : "off");
        rtc_refresh_alabel();           /* 标签仅于提交(设置)时修改 */
    }
    else                                /* time group: write the RTC clock */
    {
        int ss = 0;
        BSP_RTC_Get(NULL, NULL, NULL, NULL, NULL, &ss);
        (void)BSP_RTC_Set(s_edit[0], s_edit[1], s_edit[2], s_edit[3], s_edit[4], ss);
        PRINT_LOG("[UI  ] rtc apply %d-%d-%d %d:%d (ss=%d)\r\n",
                  s_edit[0], s_edit[1], s_edit[2], s_edit[3], s_edit[4], ss);
        rtc_refresh_clock();
    }
    rtc_refresh_fields();
}

static void rtc_alarm_on(void)
{
    s_alarm_on = 1U;
    s_alarm_ringing    = 0U;            /* a fresh enable starts a clean ring window */
    s_alarm_ring_ticks = 0U;
    s_alarm_beeping    = 0U;
    (void)BSP_RTC_Alarm_Set(s_alarm[0], s_alarm[1], 1);
    (void)BSP_RTC_Alarm_Persist(s_alarm[0], s_alarm[1], 1);
    PRINT_LOG("[UI  ] rtc alarm on %02d:%02d\r\n", s_alarm[0], s_alarm[1]);
    rtc_refresh_alabel();               /* 标签仅于提交(开启)时修改 */
}

static void rtc_alarm_off(void)
{
    s_alarm_on = 0U;
    s_alarm_ringing    = 0U;
    s_alarm_ring_ticks = 0U;
    s_alarm_beeping    = 0U;
    BSP_BEEP_Off();                     /* 闹钟关闭同时关闭蜂鸣器 */
    (void)BSP_RTC_Alarm_Set(s_alarm[0], s_alarm[1], 0);
    (void)BSP_RTC_Alarm_Persist(s_alarm[0], s_alarm[1], 0);
    PRINT_LOG("[UI  ] rtc alarm off %02d:%02d (buzzer stopped)\r\n",
              s_alarm[0], s_alarm[1]);
    rtc_refresh_alabel();               /* 标签仅于提交(关闭)时修改 */
}

/* Software alarm compare, driven by the global 2 Hz UI tick so it rings on
 * any page.  When the set time is reached the buzzer sounds for 60 s, then the
 * alarm auto-disables (persists off, silences the buzzer, refreshes label). */
void rtc_check_alarm(void)
{
    int hh, mm, ss;

    /* Ring session in progress: keep the buzzer on and count the 60 s window. */
    if (s_alarm_ringing != 0U)
    {
        s_alarm_ring_ticks++;
        if (s_alarm_ring_ticks >= 120U)   /* 60 s @ 2 Hz */
        {
            PRINT_LOG("[UI  ] alarm auto-off after 60s\r\n");
            rtc_alarm_off();              /* disables + persists off + stops buzzer */
            return;
        }
        if (s_alarm_beeping == 0U)
        {
            BSP_BEEP_Set(1);
            s_alarm_beeping = 1U;
        }
        return;
    }

    if (s_alarm_on == 0U) return;

    BSP_RTC_Get(NULL, NULL, NULL, &hh, &mm, &ss);
    if (hh == s_alarm[0] && mm == s_alarm[1] && ss < 3)
    {
        int cur_min = hh * 60 + mm;
        if (s_alarm_rung_min != cur_min)
        {
            s_alarm_rung_min = cur_min;
            s_alarm_ringing    = 1U;
            s_alarm_ring_ticks = 0U;
            s_alarm_beeping    = 0U;   /* let the ring block above start the beep */
            PRINT_LOG("[UI  ] alarm trigger %02d:%02d\r\n", hh, mm);
        }
    }
}

static lv_obj_t *mk_rtc_nav_btn(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                                lv_coord_t w, const char *title, int arrow, int code)
{
    lv_obj_t *btn = lv_btn_create(parent);

    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, w, 44);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COL_BTN), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(COL_DIM), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    if (arrow == 1 || arrow == 2)        /* up / down chevron (no font) */
    {
        lv_obj_t *line = lv_line_create(btn);
        lv_obj_remove_style_all(line);
        lv_obj_set_size(line, 20, 20);
        lv_obj_set_pos(line, (lv_coord_t)((w - 20) / 2), 12);
        lv_obj_set_style_line_width(line, 3, LV_PART_MAIN);
        lv_obj_set_style_line_color(line, lv_color_hex(COL_TXT), LV_PART_MAIN);
        lv_obj_set_style_line_rounded(line, 1, LV_PART_MAIN);
        lv_line_set_points(line, arrow == 1 ? s_arrow_up : s_arrow_down, 3);
        lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
    }
    else
    {
        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, &lv_font_gbk_16, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TXT), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_label_set_text(lbl, title);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
        lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    }

    lv_obj_add_event_cb(btn, rtc_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)code);
    return btn;
}

void build_page_rtc(void)
{
    lv_obj_t *scr  = lv_scr_act();
    lv_obj_t *band;
    lv_coord_t inner    = (lv_coord_t)(s_w - (2 * UI_PAD));
    lv_coord_t clock_y  = (lv_coord_t)(HDR_H + TOP_GAP);
    lv_coord_t clock_h  = 300;                 /* ~80% of previous half-screen */
    lv_coord_t set_y    = (lv_coord_t)(clock_y + clock_h + BAND_GAP);
    lv_coord_t set_h    = (lv_coord_t)(s_h - NAV_H - TOP_GAP - set_y);
    lv_coord_t t_gap    = 6;
    lv_coord_t t_cell_w = (lv_coord_t)((inner - (4 * t_gap)) / RTC_TFIELD_COUNT);
    lv_coord_t t_y      = 32;
    lv_coord_t t_h      = 54;
    lv_coord_t a_gap    = 26;
    lv_coord_t a_y      = (lv_coord_t)(t_y + t_h + a_gap);
    lv_coord_t a_lbl_y  = (lv_coord_t)(a_y - 20);
    lv_coord_t a_cell_w = 86;
    lv_coord_t btn_gap  = 8;
    lv_coord_t btn_h    = 44;
    lv_coord_t row_gap  = 8;
    lv_coord_t btn_margin = 12;   /* short left/right inset for the button rows      */
    lv_coord_t btn_up     = 24;   /* gap kept below the buttons (shift them upward)  */
    /* Two button rows, inset horizontally by btn_margin and lifted by btn_up:
     *   row 1 (top):    上 / 下 / 设置
     *   row 2 (bottom): 闹钟开启 / 闹钟关闭 */
    lv_coord_t btn_area_w = (lv_coord_t)(inner - (2 * btn_margin));
    lv_coord_t btn_y    = (lv_coord_t)(set_h - (2 * btn_h) - row_gap - btn_up);
    lv_coord_t btn_w3   = (lv_coord_t)((btn_area_w - (2 * btn_gap)) / 3);
    lv_coord_t btn_w2   = (lv_coord_t)((btn_area_w - btn_gap) / 2);
    lv_coord_t row2_y   = (lv_coord_t)(btn_y + btn_h + row_gap);
    int i;

    rtc_load_edit();

    /* ---- Clock: ~80% of previous height, two big centered lines. --------- */
    (void)make_band(scr, clock_y, clock_h, "实时时钟");
    p3_date = mk_label_center(scr, (lv_coord_t)(clock_y + clock_h / 2 - 64),
                              &lv_font_gbk_32, COL_TXT, "");
    p3_time = mk_label_center(scr, (lv_coord_t)(clock_y + clock_h / 2 - 2),
                              &lv_font_gbk_32, COL_TXT, "");
    rtc_refresh_clock();

    /* ---- Combined time + alarm area (unified up/down/set, no 切换). ------ */
    band = make_band(scr, set_y, set_h, "时间 / 闹钟设置");

    for (i = 0; i < RTC_TFIELD_COUNT; i++)
    {
        lv_obj_t *cell = lv_obj_create(band);
        lv_obj_t *nlbl;
        lv_obj_remove_style_all(cell);
        lv_obj_set_size(cell, t_cell_w, t_h);
        lv_obj_set_pos(cell, (lv_coord_t)(i * (t_cell_w + t_gap)), t_y);
        lv_obj_set_style_bg_color(cell, lv_color_hex(COL_BTN), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(cell, lv_color_hex(COL_DIM), LV_PART_MAIN);
        lv_obj_set_style_border_width(cell, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(cell, 4, LV_PART_MAIN);
        lv_obj_set_style_pad_all(cell, 0, LV_PART_MAIN);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        p3_tcell[i] = cell;

        nlbl = lv_label_create(cell);
        lv_obj_set_style_text_font(nlbl, &lv_font_gbk_16, LV_PART_MAIN);
        lv_obj_set_style_text_color(nlbl, lv_color_hex(COL_DIM), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(nlbl, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_label_set_text(nlbl, s_tname[i]);
        lv_obj_align(nlbl, LV_ALIGN_TOP_MID, 0, 6);

        p3_tval[i] = lv_label_create(cell);
        lv_obj_set_style_text_font(p3_tval[i], &lv_font_gbk_24, LV_PART_MAIN);
        lv_obj_set_style_text_color(p3_tval[i], lv_color_hex(COL_TXT), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(p3_tval[i], LV_OPA_TRANSP, LV_PART_MAIN);
        lv_label_set_text(p3_tval[i], "");
        lv_obj_align(p3_tval[i], LV_ALIGN_CENTER, 0, 4);

        lv_obj_add_event_cb(cell, rtc_cell_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)((0 << 8) | i));
    }

    /* Alarm: 时/分 cells (click to select) + status label. */
    (void)mk_label(band, 0, a_lbl_y, &lv_font_gbk_16, COL_DIM, "闹钟设置");
    for (i = 0; i < RTC_AFIELD_COUNT; i++)
    {
        lv_obj_t *cell = lv_obj_create(band);
        lv_obj_t *nlbl;
        lv_obj_remove_style_all(cell);
        lv_obj_set_size(cell, a_cell_w, t_h);
        lv_obj_set_pos(cell, (lv_coord_t)(i * (a_cell_w + t_gap)), a_y);
        lv_obj_set_style_bg_color(cell, lv_color_hex(COL_BTN), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(cell, lv_color_hex(COL_DIM), LV_PART_MAIN);
        lv_obj_set_style_border_width(cell, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(cell, 4, LV_PART_MAIN);
        lv_obj_set_style_pad_all(cell, 0, LV_PART_MAIN);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        p3_acell[i] = cell;

        nlbl = lv_label_create(cell);
        lv_obj_set_style_text_font(nlbl, &lv_font_gbk_16, LV_PART_MAIN);
        lv_obj_set_style_text_color(nlbl, lv_color_hex(COL_DIM), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(nlbl, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_label_set_text(nlbl, s_aname[i]);
        lv_obj_align(nlbl, LV_ALIGN_TOP_MID, 0, 6);

        p3_aval[i] = lv_label_create(cell);
        lv_obj_set_style_text_font(p3_aval[i], &lv_font_gbk_24, LV_PART_MAIN);
        lv_obj_set_style_text_color(p3_aval[i], lv_color_hex(COL_TXT), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(p3_aval[i], LV_OPA_TRANSP, LV_PART_MAIN);
        lv_label_set_text(p3_aval[i], "");
        lv_obj_align(p3_aval[i], LV_ALIGN_CENTER, 0, 4);

        lv_obj_add_event_cb(cell, rtc_cell_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)((1 << 8) | i));
    }
    p3_alabel = mk_label(band, (lv_coord_t)(2 * (a_cell_w + t_gap) + 8),
                         (lv_coord_t)(a_y + 14), &lv_font_gbk_24, COL_TXT, "");
    rtc_refresh_alabel();               /* initial committed-value display */

    rtc_refresh_fields();

    /* Bottom rows: line 1 = 上(↑) / 下(↓) / 设置 ; line 2 = 闹钟开启 / 闹钟关闭.
     * Opening / closing syncs RTC state AND persists to EEPROM. */
    (void)mk_rtc_nav_btn(band, (lv_coord_t)(btn_margin + 0 * (btn_w3 + btn_gap)), btn_y,   btn_w3, "上",       1, 0);
    (void)mk_rtc_nav_btn(band, (lv_coord_t)(btn_margin + 1 * (btn_w3 + btn_gap)), btn_y,   btn_w3, "下",       2, 1);
    (void)mk_rtc_nav_btn(band, (lv_coord_t)(btn_margin + 2 * (btn_w3 + btn_gap)), btn_y,   btn_w3, "设置",     0, 2);
    (void)mk_rtc_nav_btn(band, (lv_coord_t)(btn_margin + 0 * (btn_w2 + btn_gap)), row2_y,  btn_w2, "闹钟开启", 0, 3);
    (void)mk_rtc_nav_btn(band, (lv_coord_t)(btn_margin + 1 * (btn_w2 + btn_gap)), row2_y,  btn_w2, "闹钟关闭", 0, 4);
}

static void rtc_btn_cb(lv_event_t *e)
{
    int code = (int)(intptr_t)lv_event_get_user_data(e);

    LV_UNUSED(e);

    switch (code)
    {
        case 0: rtc_adj(+1); break;            /* 上(↑): +1 (wraps) */
        case 1: rtc_adj(-1); break;            /* 下(↓): -1 (wraps) */
        case 2: rtc_apply(); break;            /* 设置: 写 RTC 或持久化闹钟 */
        case 3: rtc_alarm_on(); break;         /* 闹钟开启: RTC+EEPROM 同步 */
        case 4: rtc_alarm_off(); break;        /* 闹钟关闭: RTC+EEPROM 同步 + 关蜂鸣器 */
        default: break;
    }
}

static void rtc_cell_cb(lv_event_t *e)
{
    int code = (int)(intptr_t)lv_event_get_user_data(e);
    int grp  = (code >> 8) & 0xFF;
    int fld  = code & 0xFF;

    LV_UNUSED(e);

    s_selgrp = grp;
    if (grp == 0) s_tfield = fld;
    else          s_afield = fld;
    rtc_refresh_fields();
}
