/**
  ******************************************************************************
  * @file    app_pages.c
  * @brief   The four LVGL pages that are not the emulator.
  *
  *    时钟      - RTC readout, the page the board boots into
  *    系统信息  - clock tree, SD geometry, LVGL heap, glyph cache
  *    按键测试  - live view of the virtual key layer; this is the page to open
  *                when checking that the Python host script actually reaches
  *                the firmware
  *    关于      - build stamp and the console command list
  *
  *  Each page only rewrites label text on its tick, so LVGL repaints a handful
  *  of dirty rectangles rather than the whole 240x240 frame.
  ******************************************************************************
  */
#include "app_page.h"
#include "lv_font_gbk.h"
#include "menu_icons.h"
#include "drv_rtc.h"
#include "drv_sdio.h"
#include "drv_oled_text.h"
#include "bsp_console.h"
#include <stdio.h>
#include <string.h>

#define TICK_PERIOD_MS      200U

/*============================================================================
 *  时钟
 *==========================================================================*/

static lv_obj_t *s_clk_time;
static lv_obj_t *s_clk_date;
static lv_obj_t *s_clk_src;
static uint32_t  s_clk_last;

static void clock_refresh(void)
{
    rtc_datetime_t dt;

    if (drv_rtc_get(&dt) != RT_OK)
    {
        lv_label_set_text(s_clk_time, "--:--:--");
        lv_label_set_text(s_clk_date, "RTC 未启动");
        return;
    }

    lv_label_set_text_fmt(s_clk_time, "%02d:%02d:%02d",
                          (int)dt.hour, (int)dt.minute, (int)dt.second);
    lv_label_set_text_fmt(s_clk_date, "%04d-%02d-%02d  %s",
                          (int)dt.year, (int)dt.month, (int)dt.day,
                          drv_rtc_weekday_cn(dt.weekday));
}

static void clock_enter(lv_obj_t *root)
{
    (void)ui_header(root, "时钟");

    s_clk_time = ui_label_center(root, 70,  &lv_font_gbk_32, 0x00E5FF, "--:--:--");
    s_clk_date = ui_label_center(root, 120, &lv_font_gbk_16, COL_TEXT, "----");
    s_clk_src  = ui_label_center(root, 160, &lv_font_gbk_12, COL_DIM, "");

    lv_label_set_text_fmt(s_clk_src, "时基 %s",
                          (drv_rtc_clock_source() == RTC_CLK_LSE)
                              ? "LSE 32.768kHz 晶振"
                              : "LSI 内部 RC (会漂移)");

    (void)ui_label_center(root, UI_H - 22, &lv_font_gbk_12, COL_DIM,
                          "B 返回主菜单");

    s_clk_last = 0U;
    clock_refresh();
}

static void clock_tick(void)
{
    uint32_t now = HAL_GetTick();

    if ((now - s_clk_last) >= 500U)
    {
        s_clk_last = now;
        clock_refresh();
    }
}

static const app_page_t s_page_clock =
{
    .title       = "时钟",
    .hint        = "RTC 日期与时间",
    .cmd         = "clock",
    .full_screen = 0U,
    .icon        = &icon_clock,
    .on_enter    = clock_enter,
    .on_exit     = NULL,
    .on_key      = NULL,
    .on_tick     = clock_tick
};

const app_page_t *page_clock_get(void)
{
    return &s_page_clock;
}

/*============================================================================
 *  系统信息
 *==========================================================================*/

static lv_obj_t *s_si_uptime;
static lv_obj_t *s_si_sd;
static lv_obj_t *s_si_bar;
static lv_obj_t *s_si_cache;
static lv_obj_t *s_si_heap;
static uint32_t  s_si_last;
static uint32_t  s_si_sd_countdown;

static void sysinfo_refresh_sd(void)
{
    sd_info_t info;
    char      used[16];
    char      total[16];
    uint32_t  pct = 0U;

    if (drv_sd_query_info(&info) != RT_OK)
    {
        lv_label_set_text(s_si_sd, "SD    读取失败");
        lv_obj_set_style_text_color(s_si_sd, lv_color_hex(COL_ERR), LV_PART_MAIN);
        lv_bar_set_value(s_si_bar, 0, LV_ANIM_OFF);
        return;
    }

    lv_obj_set_style_text_color(s_si_sd, lv_color_hex(COL_VALUE), LV_PART_MAIN);

    drv_sd_format_size(info.fs_total_bytes - info.fs_free_bytes,
                       used, sizeof(used));
    drv_sd_format_size(info.fs_total_bytes, total, sizeof(total));

    if (info.fs_total_bytes != 0U)
    {
        pct = (uint32_t)(((info.fs_total_bytes - info.fs_free_bytes) / 1024U) *
                         100U / (info.fs_total_bytes / 1024U));
        if (pct > 100U)
        {
            pct = 100U;
        }
    }

    lv_label_set_text_fmt(s_si_sd, "SD    %s  %s / %s",
                          drv_sd_fs_name(info.fs_type), used, total);
    lv_bar_set_value(s_si_bar, (int32_t)pct, LV_ANIM_OFF);
}

static void sysinfo_enter(lv_obj_t *root)
{
    lv_obj_t *lbl;

    (void)ui_header(root, "系统信息");

    lbl = ui_label(root, UI_PAD, 36, &lv_font_gbk_16, COL_LABEL, "");
    lv_label_set_text_fmt(lbl, "主频  %d MHz  (%s)",
                          (int)(HAL_RCC_GetSysClockFreq() / 1000000U),
                          (g_clock_source == CLOCK_SRC_HSE_XTAL) ? "HSE 25M"
                                                                 : "HSI 备用");

    lbl = ui_label(root, UI_PAD, 58, &lv_font_gbk_16, COL_LABEL, "");
    lv_label_set_text_fmt(lbl, "总线  HCLK %d MHz",
                          (int)(HAL_RCC_GetHCLKFreq() / 1000000U));

    s_si_uptime = ui_label(root, UI_PAD, 80, &lv_font_gbk_16, COL_LABEL,
                           "运行  00:00:00");

    ui_separator(root, 104);

    s_si_sd = ui_label(root, UI_PAD, 110, &lv_font_gbk_16, COL_VALUE,
                       "SD    读取中...");

    s_si_bar = lv_bar_create(root);
    lv_obj_remove_style_all(s_si_bar);
    lv_obj_set_size(s_si_bar, UI_W - (2 * UI_PAD), 8);
    lv_obj_set_pos(s_si_bar, UI_PAD, 134);
    lv_bar_set_range(s_si_bar, 0, 100);
    lv_bar_set_value(s_si_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_si_bar, lv_color_hex(COL_DIM), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_si_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_si_bar, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_si_bar, lv_color_hex(COL_ACCENT),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_si_bar, LV_OPA_COVER, LV_PART_INDICATOR);

    ui_separator(root, 152);

    s_si_heap = ui_label(root, UI_PAD, 158, &lv_font_gbk_16, COL_LABEL, "");
    lv_label_set_text_fmt(s_si_heap, "LVGL  v%d.%d.%d  %d KB",
                          LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR,
                          LVGL_VERSION_PATCH, (int)(LV_MEM_SIZE / 1024U));

    s_si_cache = ui_label(root, UI_PAD, 180, &lv_font_gbk_16, COL_LABEL,
                          "字库  命中 0 / 读卡 0");

    lbl = ui_label(root, UI_PAD, 202, &lv_font_gbk_12, COL_DIM, "");
    lv_label_set_text_fmt(lbl, "控制台  UART1 + USB CDC (%s)",
                          (bsp_console_usb_ready() != 0) ? "已连接" : "未连接");

    (void)ui_label_center(root, UI_H - 22, &lv_font_gbk_12, COL_DIM,
                          "B 返回主菜单");

    s_si_last         = 0U;
    s_si_sd_countdown = 0U;
}

static void sysinfo_tick(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t hits = 0U;
    uint32_t miss = 0U;
    uint32_t sec;

    if ((now - s_si_last) < 500U)
    {
        return;
    }
    s_si_last = now;

    sec = now / 1000U;
    lv_label_set_text_fmt(s_si_uptime, "运行  %02d:%02d:%02d",
                          (int)(sec / 3600U), (int)((sec / 60U) % 60U),
                          (int)(sec % 60U));

    lv_font_gbk_cache_stats(&hits, &miss);
    lv_label_set_text_fmt(s_si_cache, "字库  命中 %d / 读卡 %d",
                          (int)hits, (int)miss);

    if (s_si_sd_countdown == 0U)
    {
        sysinfo_refresh_sd();
        s_si_sd_countdown = 20U;            /* every ~10 s */
    }
    else
    {
        s_si_sd_countdown--;
    }
}

static const app_page_t s_page_sysinfo =
{
    .title       = "系统信息",
    .hint        = "时钟 / SD / 内存",
    .cmd         = "sysinfo",
    .full_screen = 0U,
    .icon        = &icon_sysinfo,
    .on_enter    = sysinfo_enter,
    .on_exit     = NULL,
    .on_key      = NULL,
    .on_tick     = sysinfo_tick
};

const app_page_t *page_sysinfo_get(void)
{
    return &s_page_sysinfo;
}

/*============================================================================
 *  按键测试
 *
 *  Eight indicator boxes laid out like a game pad plus a rolling log of the
 *  last few edges.  BACK is deliberately *not* swallowed so the page can still
 *  be left; every other key is consumed and shown.
 *==========================================================================*/

#define KT_LOG_LINES        4

static lv_obj_t *s_kt_box[KEY_COUNT];
static lv_obj_t *s_kt_log[KT_LOG_LINES];
static lv_obj_t *s_kt_mask;
static char      s_kt_text[KT_LOG_LINES][32];
static uint32_t  s_kt_last_mask;

/* Screen position of each indicator, roughly matching a real pad. */
static const lv_coord_t s_kt_pos[KEY_COUNT][2] =
{
    {  30,  50 },   /* UP     */
    {  30,  90 },   /* DOWN   */
    {  10,  70 },   /* LEFT   */
    {  50,  70 },   /* RIGHT  */
    { 190,  70 },   /* A      */
    { 155,  70 },   /* B      */
    {  95, 110 },   /* SELECT */
    { 140, 110 },   /* START  */
    {   0,   0 },   /* OK     - aliased, not drawn */
    {   0,   0 },   /* BACK   - aliased, not drawn */
    {   0,   0 }    /* MENU   - aliased, not drawn */
};

static void keytest_log(const char *line)
{
    int i;

    for (i = 0; i < (KT_LOG_LINES - 1); i++)
    {
        (void)strncpy(s_kt_text[i], s_kt_text[i + 1], sizeof(s_kt_text[0]) - 1U);
        s_kt_text[i][sizeof(s_kt_text[0]) - 1U] = '\0';
        lv_label_set_text(s_kt_log[i], s_kt_text[i]);
    }

    (void)strncpy(s_kt_text[KT_LOG_LINES - 1], line, sizeof(s_kt_text[0]) - 1U);
    s_kt_text[KT_LOG_LINES - 1][sizeof(s_kt_text[0]) - 1U] = '\0';
    lv_label_set_text(s_kt_log[KT_LOG_LINES - 1], s_kt_text[KT_LOG_LINES - 1]);
}

static void keytest_enter(lv_obj_t *root)
{
    int i;

    (void)ui_header(root, "按键测试");

    for (i = 0; i < 8; i++)
    {
        lv_obj_t *box = lv_obj_create(root);

        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, 34, 22);
        lv_obj_set_pos(box, s_kt_pos[i][0], s_kt_pos[i][1]);
        lv_obj_set_style_bg_color(box, lv_color_hex(COL_DIM), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(box, 3, LV_PART_MAIN);
        lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

        (void)ui_label(box, 3, 4, &lv_font_gbk_12, COL_TEXT,
                       bsp_key_name((key_id_t)i));

        s_kt_box[i] = box;
    }

    s_kt_mask = ui_label(root, UI_PAD, 140, &lv_font_gbk_12, COL_ACCENT,
                         "mask 0x000");

    ui_separator(root, 158);

    for (i = 0; i < KT_LOG_LINES; i++)
    {
        s_kt_text[i][0] = '\0';
        s_kt_log[i] = ui_label(root, UI_PAD, (lv_coord_t)(164 + (i * 16)),
                               &lv_font_gbk_12, COL_DIM, "");
    }

    s_kt_last_mask = 0xFFFFFFFFU;           /* force the first repaint */
    keytest_log("串口发送: key a");
}

static int keytest_key(key_id_t id, key_edge_t edge)
{
    char line[32];

    (void)snprintf(line, sizeof(line), "%-6s %s",
                   bsp_key_name(id), (edge == KEY_EV_DOWN) ? "按下" : "释放");
    keytest_log(line);

    /* Let BACK / B bubble up so the page can be closed. */
    return ((id == KEY_BACK) || (id == KEY_B)) ? 0 : 1;
}

static void keytest_tick(void)
{
    uint32_t mask = bsp_key_state();
    int      i;

    if (mask == s_kt_last_mask)
    {
        return;
    }
    s_kt_last_mask = mask;

    for (i = 0; i < 8; i++)
    {
        uint32_t bit = (uint32_t)1U << i;

        lv_obj_set_style_bg_color(s_kt_box[i],
                                  lv_color_hex(((mask & bit) != 0U) ? COL_ACCENT
                                                                    : COL_DIM),
                                  LV_PART_MAIN);
    }

    lv_label_set_text_fmt(s_kt_mask, "mask 0x%03X", (unsigned)mask);
}

static const app_page_t s_page_keytest =
{
    .title       = "按键测试",
    .hint        = "验证串口模拟按键",
    .cmd         = "keytest",
    .full_screen = 0U,
    .icon        = &icon_keytest,
    .on_enter    = keytest_enter,
    .on_exit     = NULL,
    .on_key      = keytest_key,
    .on_tick     = keytest_tick
};

const app_page_t *page_keytest_get(void)
{
    return &s_page_keytest;
}

/*============================================================================
 *  关于
 *==========================================================================*/

static void about_enter(lv_obj_t *root)
{
    (void)ui_header(root, "关于");

    (void)ui_label(root, UI_PAD, 36, &lv_font_gbk_16, COL_TEXT,
                   "LVGL 菜单 + NES 模拟器");
    (void)ui_label(root, UI_PAD, 58, &lv_font_gbk_12, COL_DIM,
                   "STM32H743ZIT6  480MHz  HSE 25MHz");
    (void)ui_label(root, UI_PAD, 74, &lv_font_gbk_12, COL_DIM,
                   "ST7789 240x240 @ SPI6   SD @ SDMMC1");
    (void)ui_label(root, UI_PAD, 90, &lv_font_gbk_12, COL_DIM,
                   __DATE__ "  " __TIME__);

    ui_separator(root, 110);

    (void)ui_label(root, UI_PAD, 116, &lv_font_gbk_16, COL_LABEL, "串口指令");
    (void)ui_label(root, UI_PAD, 138, &lv_font_gbk_12, COL_VALUE,
                   "help / status / menu");
    (void)ui_label(root, UI_PAD, 154, &lv_font_gbk_12, COL_VALUE,
                   "open <page>   back");
    (void)ui_label(root, UI_PAD, 170, &lv_font_gbk_12, COL_VALUE,
                   "key <name>  down/up <name>");
    (void)ui_label(root, UI_PAD, 186, &lv_font_gbk_12, COL_VALUE,
                   "rom list | load <n> | reset");

    (void)ui_label_center(root, UI_H - 22, &lv_font_gbk_12, COL_DIM,
                          "B 返回主菜单");
}

static const app_page_t s_page_about =
{
    .title       = "关于",
    .hint        = "版本与串口指令",
    .cmd         = "about",
    .full_screen = 0U,
    .icon        = &icon_about,
    .on_enter    = about_enter,
    .on_exit     = NULL,
    .on_key      = NULL,
    .on_tick     = NULL
};

const app_page_t *page_about_get(void)
{
    return &s_page_about;
}
