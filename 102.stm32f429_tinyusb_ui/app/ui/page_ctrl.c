/**
  ******************************************************************************
  * @file    app/ui/page_ctrl.c
  * @brief   Page 2 - device control screen.
  *
  *  Two buttons toggle the LED (PB0, low-active) and the buzzer (PCF8574 P0,
  *  low = sound).  A status band shows the live LED / buzzer state.  PB0 is
  *  driven ONLY from this page -- led_task no longer touches it (see main.c),
  *  so the button state is not clobbered.
  ******************************************************************************
  */
#include "ui_common.h"

void refresh_ctrl(void)
{
    if (s_ui.p2_led_state != NULL)
    {
        lv_label_set_text(s_ui.p2_led_state,
                          s_led_on ? "LED 状态  点亮" : "LED 状态  关闭");
        if (s_ui.p2_led_lbl != NULL)
        {
            lv_label_set_text(s_ui.p2_led_lbl, s_led_on ? "LED 开" : "LED 关");
        }
    }
    if (s_ui.p2_beep_state != NULL)
    {
        lv_label_set_text(s_ui.p2_beep_state,
                          s_beep_on ? "蜂鸣器  鸣响" : "蜂鸣器  静音");
        if (s_ui.p2_beep_lbl != NULL)
        {
            lv_label_set_text(s_ui.p2_beep_lbl,
                              s_beep_on ? "蜂鸣器 开" : "蜂鸣器 关");
        }
    }
}

void build_page_ctrl(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_coord_t bw;
    lv_coord_t bh;
    lv_coord_t by;
    lv_coord_t bx0;
    lv_coord_t bx1;
    lv_coord_t st_y;
    lv_coord_t st_h;
    lv_obj_t  *st;

    /* Two side-by-side control buttons under the header. */
    bw  = (lv_coord_t)((s_w - (3 * UI_PAD)) / 2);
    bh  = 200;
    by  = (lv_coord_t)(HDR_H + TOP_GAP + 16);
    bx0 = UI_PAD;
    bx1 = (lv_coord_t)(UI_PAD + bw + UI_PAD);

    s_ui.p2_led_lbl  = mk_ctrl_button(scr, bx0, by, bw, bh, "LED", 1);
    s_ui.p2_led_btn  = lv_obj_get_parent(s_ui.p2_led_lbl);
    s_ui.p2_beep_lbl = mk_ctrl_button(scr, bx1, by, bw, bh, "蜂鸣器", 2);
    s_ui.p2_beep_btn = lv_obj_get_parent(s_ui.p2_beep_lbl);

    /* Status band fills the rest of the content area down to the nav bar. */
    st_y = (lv_coord_t)(by + bh + BAND_GAP);
    st_h = (lv_coord_t)(s_h - st_y - NAV_H - TOP_GAP);
    if (st_h < (3 * BAND_GAP))
    {
        st_h = (lv_coord_t)(3 * BAND_GAP);
    }

    st = make_band(scr, st_y, st_h, "状态");
    s_ui.p2_led_state  = mk_label(st, 8, 32, &lv_font_gbk_16, COL_TXT,
                                  s_led_on ? "LED 状态  点亮" : "LED 状态  关闭");
    s_ui.p2_beep_state = mk_label(st, 8, 56, &lv_font_gbk_16, COL_TXT,
                                  s_beep_on ? "蜂鸣器  鸣响" : "蜂鸣器  静音");
    (void)mk_label(st, 8, 80, &lv_font_gbk_16, COL_DIM,
                   "LED = PB0  蜂鸣器 = PCF8574 P0");
    (void)mk_label(st, 8, 104, &lv_font_gbk_16, COL_DIM,
                   "左右箭头切换界面");
}
