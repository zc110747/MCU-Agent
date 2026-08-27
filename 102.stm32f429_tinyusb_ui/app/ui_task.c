/**
  ******************************************************************************
  * @file    ui_task.c
  * @brief   LVGL rendering thread for the STM32F429 800x400 LCD panel.
  *
  *  Flow (runs entirely after the FreeRTOS scheduler is up):
  *    1. lcd_driver_init()  - FMC Bank1 NE1 + panel init (backlight on, black).
  *    2. lv_init() + lv_port_disp_init() - register the 800x400 panel buffer
  *       (draw buffer lives in SDRAM).
  *    3. wait for g_usb_state == USB_MOUNTED (the U-disk holds the GBK fonts).
  *    4. lcd_driver_font_init() - mount 0: and open the GBKxx.FON / UNIGBK.BIN
  *       files sitting in 0:/SYSTEM/FONT/.
  *    5. app_ui_create() if the fonts are at least partially ready, otherwise
  *       app_ui_show_fault() (ASCII-only error page).
  *    6. loop: every LVGL_TASK_PERIOD_MS call lv_timer_handler().
  *
  *  When the U-disk state changes the screen is rebuilt so a "plug in the disk"
  *  prompt turns into the live panel without a reset.
  ******************************************************************************
  */
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "lvgl.h"
#include "lv_font_gbk.h"

#include "bsp_lcd.h"
#include "bsp_lcd_text.h"
#include "lv_port_disp.h"
#include "usb_host_app.h"
#include "app_ui.h"

#include "ui_task.h"

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */
static void show_screen_for_font(void)
{
    uint32_t mask = lcd_driver_font_status();

    if (mask == 0U)
    {
        /* No font file found at all: tell the user where to put them. */
        app_ui_show_fault(
            "未找到字库文件",
            "请将字库放入 U 盘:",
            "0:/SYSTEM/FONT/ 下 GBKxx.FON");
    }
    else
    {
        /* At least one GBKxx.FON is open; the Chinese title will render. */
        app_ui_create();
    }
}

/* -------------------------------------------------------------------------- */
/* Task body                                                                  */
/* -------------------------------------------------------------------------- */
void ui_task(void *arg)
{
    (void)arg;

    printf("[UI  ] starting LCD + LVGL bring-up\r\n");

    /* 1. Panel hardware.  Safe to do immediately; needs no USB. */
    if (lcd_driver_init() != RT_OK)
    {
        printf("[UI  ] LCD driver init FAILED - parking task\r\n");
        for (;;)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    /* 2. LVGL core + display port.  The draw buffer is pvPortMalloc'd in SDRAM
     *    by lv_port_disp_init(); the 5 ms render pump is below. */
    lv_init();
    lv_port_disp_init();

    /* 3. Wait for the U-disk (GBK fonts live there).  app_ui_show_fault() is
     *    the "please insert the disk" prompt while we wait. */
    printf("[UI  ] waiting for U-disk (GBK fonts)...\r\n");
    app_ui_show_fault("等待 U 盘字库...",
                      "插入含字库的 U 盘",
                      "0:/SYSTEM/FONT/GBKxx.FON");

    usb_state_t last_usb = USB_DISCONNECTED;

    for (;;)
    {
        usb_state_t cur = g_usb_state;

        if (cur != last_usb)
        {
            if (cur == USB_MOUNTED)
            {
                /* 4. Fonts are now reachable. */
                printf("[FONT] mounting U-disk fonts (0:/SYSTEM/FONT/)\r\n");
                if (lcd_driver_font_init() == RT_OK)
                {
                    uint32_t mask = lcd_driver_font_status();
                    printf("[FONT] font status mask = 0x%02X\r\n",
                           (unsigned int)mask);
                    show_screen_for_font();
                }
                else
                {
                    printf("[FONT] font init FAILED\r\n");
                    app_ui_show_fault("字库挂载失败",
                                      "U 盘文件系统无法挂载",
                                      "请检查 U 盘格式 (exFAT/FAT32)");
                }
            }
            else if (cur == USB_ERROR)
            {
                app_ui_show_fault("U 盘错误",
                                  "无法读取 U 盘",
                                  "请重新插入 U 盘");
            }
            else if (cur != USB_MOUNTED)
            {
                /* Disk removed / not yet ready: keep the prompt live. */
                app_ui_show_fault("等待 U 盘字库...",
                                  "插入含字库的 U 盘",
                                  "0:/SYSTEM/FONT/GBKxx.FON");
                app_ui_request_usb_refresh();
            }
            last_usb = cur;
        }

        /* 5. Render pump.  LV_TICK_CUSTOM rides on HAL_GetTick(), so no
         *    lv_tick_inc() is required here. */
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(LVGL_TASK_PERIOD_MS));
    }
}
