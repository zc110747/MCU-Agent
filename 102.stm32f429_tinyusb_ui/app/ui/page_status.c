/**
  ******************************************************************************
  * @file    app/ui/page_status.c
  * @brief   Page 0 - system status screen.
  *
  *  The three-band device status screen: 系统初始化 / 运行信息 / 故障·消息.
  *  Builds its widgets in build_page_status() and publishes live values from
  *  refresh_usb() / refresh_font() / refresh_sd() / refresh_runtime(), which the
  *  core 2 Hz tick calls while this page is on screen.
  ******************************************************************************
  */
#include "ui_common.h"

static const char *usb_state_str(usb_state_t s)
{
    switch (s)
    {
        case USB_DISCONNECTED: return "未连接";
        case USB_CONNECTED:     return "已连接";
        case USB_ENUMERATED:    return "已枚举";
        case USB_MSC_READY:     return "MSC 就绪";
        case USB_MOUNTED:       return "已挂载";
        case USB_ERROR:         return "错误";
        default:                return "未知";
    }
}

void refresh_usb(void)
{
    if (s_ui.p0_usb != NULL)
    {
        lv_label_set_text_fmt(s_ui.p0_usb, "USB 状态  %s", usb_state_str(g_usb_state));
    }
}

void refresh_font(void)
{
    uint32_t mask = lcd_driver_font_status();
    const char *src = lcd_driver_font_source();
    const char *status;

    if (s_ui.p0_font == NULL)
    {
        return;
    }

    if (mask == 0U)
    {
        lv_label_set_text(s_ui.p0_font, "字库  未加载");
        return;
    }

    if (src[0] == '1')
    {
        status = "字库  SD 卡 GBK12/16/24/32 已就绪";
    }
    else if (src[0] == '0')
    {
        status = "字库  U 盘 GBK12/16/24/32 已就绪";
    }
    else if ((mask & (FONT_MASK_GBK12 | FONT_MASK_GBK16 |
                      FONT_MASK_GBK24 | FONT_MASK_GBK32)) ==
             (FONT_MASK_GBK12 | FONT_MASK_GBK16 |
              FONT_MASK_GBK24 | FONT_MASK_GBK32))
    {
        status = "字库  GBK12/16/24/32 已就绪";
    }
    else
    {
        status = "字库  部分就绪";
    }
    lv_label_set_text(s_ui.p0_font, status);
}

void refresh_sd(void)
{
    if (s_ui.p0_sd == NULL)
    {
        return;
    }

    if (sd_card_is_ready() != 0)
    {
        lv_label_set_text_fmt(s_ui.p0_sd, "SD 卡  %lu MB (SDIO 4bit)",
                              (unsigned long)sd_card_capacity_mb());
    }
    else
    {
        lv_label_set_text(s_ui.p0_sd, "SD 卡  未检测到");
    }
}

void refresh_runtime(void)
{
    uint32_t hits = 0U;
    uint32_t miss = 0U;

    if (s_ui.p0_uptime != NULL)
    {
        lv_label_set_text_fmt(s_ui.p0_uptime, "运行  %02d:%02d:%02d",
                              (int)(s_uptime_sec / 3600U),
                              (int)((s_uptime_sec / 60U) % 60U),
                              (int)(s_uptime_sec % 60U));
    }

    if (s_ui.p0_cache != NULL)
    {
        lv_font_gbk_cache_stats(&hits, &miss);
        lv_label_set_text_fmt(s_ui.p0_cache, "缓存  命中 %d / 读卡 %d",
                              (int)hits, (int)miss);
    }
}

void build_page_status(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_t *b1;
    lv_obj_t *b2;
    lv_obj_t *b3;
    LCD_INFO *info;

    /* Band 1 - 系统初始化 (hardware init summary). */
    b1 = make_band(scr, s_band_y[0], s_band_h, "系统初始化");
    info = get_lcd_info();
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "LCD 控制器  ID = 0x%04X",
                 (unsigned int)((info != NULL) ? info->lcd_id : 0U));
        (void)mk_label(b1, 8, 32, &lv_font_gbk_16, COL_TXT, buf);
        s_ui.p0_sd = mk_label(b1, 8, 56, &lv_font_gbk_16, COL_TXT, "SD 卡  --");
        (void)mk_label(b1, 8, 80, &lv_font_gbk_16, COL_TXT, "USB 主机  已初始化");
        (void)mk_label(b1, 8, 104, &lv_font_gbk_16, COL_DIM, "LVGL 渲染  已就绪");
    }

    /* Band 2 - 运行信息. */
    b2 = make_band(scr, s_band_y[1], s_band_h, "运行信息");
    s_ui.p0_usb   = mk_label(b2, 8, 32,  &lv_font_gbk_16, COL_TXT, "USB 状态  --");
    s_ui.p0_font  = mk_label(b2, 8, 56,  &lv_font_gbk_16, COL_TXT, "字库  --");
    s_ui.p0_freq  = mk_label(b2, 8, 80,  &lv_font_gbk_16, COL_TXT, "");
    lv_label_set_text_fmt(s_ui.p0_freq, "主频  %d MHz",
                          (int)(HAL_RCC_GetSysClockFreq() / 1000000U));
    s_ui.p0_uptime = mk_label(b2, 8, 104, &lv_font_gbk_16, COL_TXT, "运行  00:00:00");
    s_ui.p0_cache  = mk_label(b2, 8, 128, &lv_font_gbk_16, COL_DIM, "缓存  命中 0 / 读卡 0");

    /* Band 3 - 故障/消息. */
    b3 = make_band(scr, s_band_y[2], s_band_h, "故障 / 消息");
    s_ui.p0_f1 = mk_label(b3, 8, 32, &lv_font_gbk_16, COL_TXT, "系统正常");
    s_ui.p0_f2 = mk_label(b3, 8, 56, &lv_font_gbk_16, COL_TXT, "");
    s_ui.p0_f3 = mk_label(b3, 8, 80, &lv_font_gbk_16, COL_TXT, "");
}
