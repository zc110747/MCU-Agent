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

#include "drv_spi_oled.h"
#include "drv_rtc.h"
#include "drv_sdio.h"
#include "drv_oled_text.h"
#include "emwin_font_gbk.h"
#include "app_ui.h"

static int g_inited = 0;

void application_init(void)
{
    /* 1. Panel (ST7789 over SPI6) */
    driver_spi_oled_init();

    /* 2. RTC wall clock (LSE with LSI fallback) */
    drv_rtc_init();

    /* 3. SD card + Chinese font files (mount "1:" + open GBKxx.FON) */
    drv_sdcard_init();
    lcd_driver_font_init();

    /* 4. emWin */
    /* STemWin's GUI_Init() runs an internal hardware-CRC integrity check.
       The CRC peripheral clock (on AHB4) MUST be enabled, otherwise that
       check never passes and GUI_Init() spins forever in its entry guard. */
    __HAL_RCC_CRC_CLK_ENABLE();
    GUI_Init();
    GUI_UC_SetEncodeUTF8();      /* decode the UTF-8 strings we draw       */
    GUI_SetTextMode(GUI_TM_TRANS);/* text never paints a background box   */
    GUI_SetDefaultFont(&EMWIN_FONT_GBK16);

    /* 5. Build the static UI */
    app_ui_init();

    g_inited = 1;
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
    }
}
