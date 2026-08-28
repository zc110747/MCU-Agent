/**
  ******************************************************************************
  * @file    app_main.c
  * @brief   Application boot + run loop for the emWin OLED info panel.
  *
  *  Boot order mirrors the 003 LVGL project:
  *    panel -> RTC -> SD card + fonts -> GUI_Init -> build UI -> refresh loop.
  *  The UI itself is redrawn at 1 Hz and flushed to the SPI6 panel; the panel
  *  keeps its own GRAM so a single flush per update is sufficient.
  ******************************************************************************
  */
#include "app_main.h"
#include "GUI.h"

#include <stdio.h>

#include "drv_spi_oled.h"
#include "drv_rtc.h"
#include "drv_sdio.h"
#include "drv_oled_text.h"
#include "emwin_font_gbk.h"
#include "app_ui.h"

static int g_inited = 0;

void application_init(void)
{
    printf("[boot] STM32H743 emWin panel start\r\n");

    /* 1. Panel (ST7789 over SPI6) */
    driver_spi_oled_init();
    printf("[boot] OLED (ST7789/SPI6) init ok\r\n");

    /* 2. RTC wall clock (LSE with LSI fallback) */
    drv_rtc_init();
    printf("[boot] RTC init ok (src=%s)\r\n",
           (drv_rtc_clock_source() == RTC_CLK_LSE) ? "LSE" :
           (drv_rtc_clock_source() == RTC_CLK_LSI) ? "LSI" : "NONE");

    /* 3. SD card + Chinese font files (mount "1:" + open GBKxx.FON) */
    drv_sdcard_init();
    GlobalType_t font_res = lcd_driver_font_init();
    uint32_t font_mask = lcd_driver_font_status();
    printf("[boot] SD fonts %s (mask=0x%lx)\r\n",
           (font_res == RT_OK) ? "ok" : "MISSING", (unsigned long)font_mask);

    /* 4. emWin */
    /* STemWin's GUI_Init() runs an internal hardware-CRC integrity check.
       The CRC peripheral clock (on AHB4) MUST be enabled, otherwise that
       check never passes and GUI_Init() spins forever in its entry guard. */
    __HAL_RCC_CRC_CLK_ENABLE();
    GUI_Init();
    GUI_UC_SetEncodeUTF8();      /* decode the UTF-8 strings we draw       */
    GUI_SetTextMode(GUI_TM_TRANS);/* text never paints a background box   */
    GUI_SetDefaultFont(&EMWIN_FONT_GBK16);
    printf("[boot] GUI_Init ok (clock=%s)\r\n",
           (g_clock_source == CLOCK_SRC_HSE_XTAL) ? "HSE 480M" : "HSI 64M");

    /* 5. Build the static UI */
    app_ui_init();

    g_inited = 1;
    printf("[boot] UI built, entering main loop\r\n");
}

void application_run(void)
{
    static uint32_t last_ms = 0U;
    uint32_t now = HAL_GetTick();
    int changed = 0;

    /* Redraw the model ~once per second (and immediately on the first call). */
    if ((now - last_ms) >= 1000U || last_ms == 0U)
    {
        last_ms = now;
        app_ui_refresh();
        changed = 1;
    }

    if (g_inited && changed)
    {
        emwin_flush_vram();

        /* 1 Hz heartbeat on the debug UART (USART1 PA9/PA10, 115200-8-N-1) */
        uint32_t hit = 0U, rd = 0U;
        emwin_font_get_cache_stats(&hit, &rd);
        printf("[tick] up=%lu s font=0x%lx cache hit=%lu rd=%lu\r\n",
               (unsigned long)(now / 1000U),
               (unsigned long)lcd_driver_font_status(),
               (unsigned long)hit, (unsigned long)rd);
    }
}
