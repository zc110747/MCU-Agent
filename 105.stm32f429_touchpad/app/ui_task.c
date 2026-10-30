/**
 ******************************************************************************
 * @file    ui_task.c
 * @brief   Bring up the panel + LVGL, find the SD card, build the screen.
 *
 *  Boot order (all of it inside this task, after the scheduler is running):
 *
 *      lcd_driver_init()            FMC SRAM + NT35510 probe, backlight on
 *      lv_init() + ports            display flush -> lcd_color_fill()
 *      splash text                  pure ASCII: no card needed yet
 *      sd_card_init()               SDIO + FatFs "0:"        (retried)
 *      lcd_driver_font_init("0:")   0:/SYSTEM/FONT/GBKxx.FON
 *      app_ui_create()              real screen
 *      pump                         lv_timer_handler() + gesture events
 *
 *  The card is probed every UI_PROBE_MS until it answers.  If it never does the
 *  screen is built anyway after UI_BOOT_TIMEOUT_MS (ASCII only) so the board is
 *  not bricked by a missing card, and the probe keeps running: plugging a card
 *  in rebuilds the screen with fonts and images.
 ******************************************************************************
 */
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "lvgl.h"

#include "bsp_lcd.h"
#include "bsp_lcd_text.h"
#include "lv_font_gbk.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

#include "sd_card.h"
#include "fs_diskio.h"
#include "app_ui.h"
#include "ui_task.h"
#include "bsp_uart.h"
#include "log.h"

#define UI_SPLASH_PROBE   "booting: probing sdcard 0: ..."
#define UI_SPLASH_NOCARD  "no sdcard - insert card, retrying ..."

void ui_task(void *arg)
{
    uint32_t boot_ms      = 0U;
    uint8_t  ui_built     = 0U;
    uint8_t  storage_ok   = 0U;
    uint8_t  build_needed = 0U;

    (void)arg;

    /* ---- Panel ---------------------------------------------------------- */
    if (lcd_driver_init() != RT_OK)
    {
        PRINT_LOG("[UI  ] LCD init FAILED - no panel, task parked\r\n");
        for (;;)
        {
            vTaskDelay(pdMS_TO_TICKS(1000U));
        }
    }
    PRINT_LOG("[UI  ] LCD %ux%u ready\r\n",
              (unsigned int)LCD_Width, (unsigned int)LCD_Height);

    /* ---- LVGL ----------------------------------------------------------- */
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();
    PRINT_LOG("[UI  ] LVGL %d.%d.%d up\r\n",
              LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

    app_ui_boot(UI_SPLASH_PROBE);
    lv_timer_handler();

    boot_ms = HAL_GetTick();

    for (;;)
    {
        /* ---- Storage probe ---------------------------------------------- */
        if ((storage_ok == 0U) && (sd_card_init() == RT_OK))
        {
            storage_ok = 1U;

            if (lcd_driver_font_init(FS_VOL_SD) == RT_OK)
            {
                lv_font_gbk_reset_cache();
                /* Read the status inline (not into a local) so -Wunused-but-set
                 * -variable stays quiet when PRINT_LOG is compiled out. */
                PRINT_LOG("[UI  ] GBK glyph files: %s (mask=0x%02lX)\r\n",
                          lcd_driver_font_source(),
                          (unsigned long)lcd_driver_font_status());
            }
            else
            {
                PRINT_LOG("[UI  ] no GBK glyph file on the card "
                          "(0:/SYSTEM/FONT/GBKxx.FON) - CJK will be blank\r\n");
            }

            build_needed = 1U;
        }

        /* ---- Build (or rebuild) the screen ------------------------------ */
        if ((ui_built == 0U) && ((HAL_GetTick() - boot_ms) >= UI_BOOT_TIMEOUT_MS))
        {
            build_needed = 1U;
        }

        if (build_needed != 0U)
        {
            app_ui_create();
            ui_built     = 1U;
            build_needed = 0U;
            lv_timer_handler();
            PRINT_LOG("[UI  ] screen built (%s)\r\n",
                      (storage_ok != 0U) ? "storage ready" : "no storage");
        }

        if (ui_built == 0U)
        {
            app_ui_boot((HAL_GetTick() - boot_ms >= 3000U) ? UI_SPLASH_NOCARD
                                                           : UI_SPLASH_PROBE);
        }

        /* ---- Serial console commands (USART1, one char each) ------------- */
        {
            uint8_t rx[16];
            int     n = uart_read(rx, (int)sizeof(rx));

            for (int i = 0; i < n; i++)
            {
                uint8_t ch = rx[i];

                /* Ignore CR/LF and other control bytes: commands are single
                 * characters, so a host may send "s\r\n" or just "s". */
                if ((ch >= ' ') && (ch < 0x7FU))
                {
                    app_ui_command((char)ch);
                }
            }
        }

        /* ---- Pump -------------------------------------------------------- */
        lv_timer_handler();
        app_ui_poll();

        vTaskDelay(pdMS_TO_TICKS((ui_built != 0U) ? UI_TICK_MS : UI_PROBE_MS));
    }
}
