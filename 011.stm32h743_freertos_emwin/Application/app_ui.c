/**
  ******************************************************************************
  * @file    app_ui.c
  * @brief   UI drawing for the emWin OLED info panel.
  *
  *  This is a faithful port of the 003 LVGL panel: same 240x240 layout,
  *  same colour palette and the same boot/FONT-missing fault page.  The only
  *  difference is the colour encoding: emWin's GUICC_565 stores a GUI_COLOR as
  *  0x00BBGGRR (GUI_USE_ARGB=0), while the 003 literals are 0x00RRGGBB, so
  *  ui_col() swaps R and B before the value reaches GUI_SetColor().
  ******************************************************************************
  */
#include "app_ui.h"
#include "GUI.h"

#include "main.h"
#include "drv_rtc.h"
#include "drv_sdio.h"
#include "drv_oled_text.h"
#include "emwin_font_gbk.h"

/* ---- palette (003 LVGL values, 0x00RRGGBB) ----------------------------- */
#define COL_BG       0x00000000
#define COL_HDR      0x0A3D62
#define COL_HDR_TXT  0xFFD966
#define COL_CLOCK    0x00E5FF
#define COL_DATE     0xFFFFFF
#define COL_LABEL    0x8A8A8A
#define COL_VALUE    0x40E070
#define COL_ACCENT   0xFFA000
#define COL_BAR_BG   0x2A2A2A
#define COL_SEP      0x243447
#define COL_DIM      0x606060
#define COL_ERR      0xFF4040

/* fault-page colours (also 0x00RRGGBB) */
#define COL_FAULT_HDR 0x7A1010
#define COL_FAULT_CY  0x80D0FF
#define COL_FAULT_GR  0xB0B0B0

/* ---- geometry (003 LVGL layout, 240x240) ------------------------------- */
#define UI_W       240
#define UI_H       240
#define UI_PAD     8
#define HDR_H      28
#define CLOCK_Y    34
#define DATE_Y     74
#define SEP1_Y     98
#define SD_HEAD_Y  104
#define SD_VAL_Y   124
#define SD_BAR_Y   146
#define SD_BAR_H   8
#define SEP2_Y     160
#define INFO1_Y    166
#define INFO2_Y    186
#define INFO3_Y    206
#define INFO4_Y    226

/* emWin GUICC_565 (GUI_USE_ARGB=0) stores colour as 0x00BBGGRR, so swap R/B
   relative to the 0x00RRGGBB literals above. */
static GUI_COLOR ui_col(uint32_t c)
{
    return (GUI_COLOR)((((c) & 0x000000FFU) << 16) |
                        ((c) & 0x0000FF00U)        |
                       (((c) & 0x00FF0000U) >> 16));
}

static void ui_set_color(uint32_t c) { GUI_SetColor(ui_col(c)); }

/* ------------------------------------------------------------------------- */
void app_ui_init(void)
{
    /* Warm the SD free-space cache so the first 1 Hz refresh is fast.
       drv_sd_query_info() walks the FAT only on its very first call. */
    sd_info_t info;
    (void)drv_sd_query_info(&info);
}

/* ---- SD / FONT missing fault page -------------------------------------- */
static void draw_fault_page(void)
{
    GUI_SetBkColor(ui_col(COL_BG));
    GUI_Clear();

    GUI_SetColor(ui_col(COL_FAULT_HDR));
    GUI_FillRect(0, 0, UI_W - 1, HDR_H - 1);

    GUI_SetColor(ui_col(COL_HDR_TXT));
    GUI_SetFont(&EMWIN_FONT_GBK16);
    GUI_DispStringHCenterAt("SD / FONT ERROR", UI_W / 2, 6);

    GUI_SetColor(ui_col(0xFFFFFF));
    GUI_DispStringAt("SD card fonts missing.", UI_PAD, 50);
    GUI_DispStringAt("Insert card with:", UI_PAD, 74);

    GUI_SetColor(ui_col(COL_FAULT_CY));
    GUI_SetFont(&EMWIN_FONT_GBK12);
    GUI_DispStringAt("Expected on the card:", UI_PAD, 140);

    GUI_SetColor(ui_col(COL_FAULT_GR));
    GUI_DispStringAt("1:/SYSTEM/FONT/UNIGBK.BIN", UI_PAD, 158);
    GUI_DispStringAt("1:/SYSTEM/FONT/GBK12.FON",  UI_PAD, 174);
    GUI_DispStringAt("1:/SYSTEM/FONT/GBK16.FON",  UI_PAD, 190);
    GUI_DispStringAt("1:/SYSTEM/FONT/GBK24.FON",  UI_PAD, 206);
    GUI_DispStringAt("1:/SYSTEM/FONT/GBK32.FON",  UI_PAD, 222);
}

/* ---- normal info panel -------------------------------------------------- */
static void draw_panel(const rtc_datetime_t * dt,
                       const sd_info_t * info,
                       uint8_t rtc_src,
                       int cpu_mhz,
                       int up_h, int up_m, int up_s,
                       int percent,
                       const char * used_str,
                       const char * total_str)
{
    char buf[32];

    GUI_SetBkColor(ui_col(COL_BG));
    GUI_Clear();

    /* ---- header ---- */
    ui_set_color(COL_HDR);
    GUI_FillRect(0, 0, UI_W - 1, HDR_H - 1);
    ui_set_color(COL_HDR_TXT);
    GUI_SetFont(&EMWIN_FONT_GBK16);
    GUI_DispStringHCenterAt("STM32H743 信息面板", UI_W / 2, 6);

    /* ---- clock ---- */
    GUI_SetFont(&EMWIN_FONT_GBK32);
    ui_set_color(COL_CLOCK);
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", dt->hour, dt->minute, dt->second);
    GUI_DispStringHCenterAt(buf, UI_W / 2, CLOCK_Y);

    /* ---- date + weekday ---- */
    GUI_SetFont(&EMWIN_FONT_GBK16);
    ui_set_color(COL_DATE);
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d  %s",
             dt->year, dt->month, dt->day, drv_rtc_weekday_cn(dt->weekday));
    GUI_DispStringHCenterAt(buf, UI_W / 2, DATE_Y);

    /* ---- separators ---- */
    ui_set_color(COL_SEP);
    GUI_DrawHLine(SEP1_Y, 0, UI_W - 1);
    GUI_DrawHLine(SEP2_Y, 0, UI_W - 1);

    /* ---- SD capacity header ---- */
    GUI_SetFont(&EMWIN_FONT_GBK16);
    ui_set_color(COL_LABEL);
    GUI_DispStringAt("SD卡容量", UI_PAD, SD_HEAD_Y);
    const char * cn = drv_sd_card_name(info->card_type);
    const char * fn = drv_sd_fs_name(info->fs_type);
    snprintf(buf, sizeof(buf), "%s %s", cn, fn);
    ui_set_color(COL_DIM);
    GUI_DispStringAt(buf, UI_W - UI_PAD - GUI_GetStringDistX(buf), SD_HEAD_Y);

    /* ---- SD used / total ---- */
    ui_set_color(COL_VALUE);
    snprintf(buf, sizeof(buf), "%s / %s", used_str, total_str);
    GUI_DispStringAt(buf, UI_PAD, SD_VAL_Y);

    /* ---- percentage (right, 12px) ---- */
    GUI_SetFont(&EMWIN_FONT_GBK12);
    ui_set_color(COL_ACCENT);
    snprintf(buf, sizeof(buf), "%d%%", percent);
    GUI_DispStringAt(buf, UI_W - UI_PAD - GUI_GetStringDistX(buf), SD_VAL_Y);

    /* ---- SD usage bar ---- */
    {
        int bar_x0 = UI_PAD;
        int bar_x1 = UI_W - UI_PAD - 40;     /* reserve room for "%" on right */
        ui_set_color(COL_BAR_BG);
        GUI_FillRect(bar_x0, SD_BAR_Y, bar_x1, SD_BAR_Y + SD_BAR_H - 1);
        int inner  = bar_x1 - bar_x0 + 1;
        int fill_w = (inner * percent) / 100;
        if (fill_w > 0)
        {
            ui_set_color(COL_ACCENT);
            GUI_FillRect(bar_x0, SD_BAR_Y, bar_x0 + fill_w - 1, SD_BAR_Y + SD_BAR_H - 1);
        }
    }

    /* ---- board line 1: 主频 / clock source ---- */
    GUI_SetFont(&EMWIN_FONT_GBK16);
    ui_set_color(COL_LABEL);
    GUI_DispStringAt("主频", UI_PAD, INFO1_Y);
    const char * clksrc = (g_clock_source == CLOCK_SRC_HSE_XTAL) ? "HSE 25M" : "HSI 备用";
    ui_set_color(COL_DIM);
    GUI_DispStringAt(clksrc, UI_W - UI_PAD - GUI_GetStringDistX(clksrc), INFO1_Y);
    ui_set_color(COL_VALUE);
    snprintf(buf, sizeof(buf), "%d MHz", cpu_mhz);
    GUI_DispStringAt(buf, UI_PAD + 40, INFO1_Y);

    /* ---- board line 2: uptime ---- */
    ui_set_color(COL_LABEL);
    GUI_DispStringAt("运行", UI_PAD, INFO2_Y);
    ui_set_color(COL_VALUE);
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", up_h, up_m, up_s);
    GUI_DispStringAt(buf, UI_PAD + 40, INFO2_Y);

    /* ---- board line 3: font status / RTC time base ---- */
    ui_set_color(COL_LABEL);
    GUI_DispStringAt("字库 SD卡 GBK", UI_PAD, INFO3_Y);
    const char * base = (rtc_src == RTC_CLK_LSE) ? "LSE"
                       : (rtc_src == RTC_CLK_LSI) ? "LSI" : "NONE";
    uint32_t font_mask = lcd_driver_font_status();
    ui_set_color((font_mask == 0U) ? COL_ERR : COL_DIM);
    GUI_DispStringAt(base, UI_W - UI_PAD - GUI_GetStringDistX(base), INFO3_Y);

    /* ---- board line 4: glyph cache stats (12px) ---- */
    uint32_t hit = 0U, read = 0U;
    emwin_font_get_cache_stats(&hit, &read);
    GUI_SetFont(&EMWIN_FONT_GBK12);
    ui_set_color(COL_DIM);
    snprintf(buf, sizeof(buf), "缓存 命中 %lu / 读卡 %lu",
             (unsigned long)hit, (unsigned long)read);
    GUI_DispStringAt(buf, UI_PAD, INFO4_Y);
}

void app_ui_refresh(void)
{
    rtc_datetime_t dt;
    sd_info_t info;
    uint32_t font_mask = lcd_driver_font_status();

    drv_rtc_get(&dt);
    drv_sd_query_info(&info);

    uint8_t rtc_src = (uint8_t)drv_rtc_clock_source();
    int cpu_mhz = (g_clock_source == CLOCK_SRC_HSE_XTAL) ? 480 : 64;

    uint32_t uptime_s = HAL_GetTick() / 1000U;
    int up_h = (int)(uptime_s / 3600U);
    int up_m = (int)((uptime_s % 3600U) / 60U);
    int up_s = (int)(uptime_s % 60U);

    int percent = 0;
    char used_str[16];
    char total_str[16];
    used_str[0]  = '\0';
    total_str[0] = '\0';
    if (info.valid && info.fs_total_bytes > 0U)
    {
        uint64_t used = info.fs_total_bytes - info.fs_free_bytes;
        drv_sd_format_size(used, used_str, sizeof(used_str));
        drv_sd_format_size(info.fs_total_bytes, total_str, sizeof(total_str));
        percent = (int)((used * 100U) / info.fs_total_bytes);
    }

    if (font_mask == 0U)
    {
        draw_fault_page();
        return;
    }

    draw_panel(&dt, &info, rtc_src, cpu_mhz, up_h, up_m, up_s,
               percent, used_str, total_str);
}
