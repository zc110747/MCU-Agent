/**
  ******************************************************************************
  * @file    main.c
  * @brief   STM32H743 OLED + LVGL demo entry point.
  *
  *  Boot order:
  *    1. Turn the OLED backlight on (PG12, gpio-leds "oled_bl").
  *    2. Mount the SD card and open the font files (UNIGBK.BIN + GBKxx.FON).
  *    3. Register the SD-backed GBK fonts with LVGL (lv_port_font_init).
  *    4. Build the UI (ui_show): Zephyr/LVGL version, core clock, uptime,
  *       SD font status, LED heartbeat status.
  *    5. Pump LVGL from this thread: lv_timer_handler() flushes the ST7789 via
  *       the Zephyr LVGL glue.  LVGL's clock is sourced from Zephyr's
  *       k_uptime_get_32() (LV_TICK_CUSTOM), so no lv_tick_inc() call is needed.
  *       A 500 ms heartbeat blink on the board user LED confirms liveness.
  ******************************************************************************
  */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/led.h>
#include <zephyr/sys/printk.h>
#include <zephyr/version.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <stdio.h>
#include <lvgl.h>

#include "drv_oled_text.h"
#include "lv_port_font.h"
#include "ui.h"

/* LVGL heap lock owned by the stress tests (app/stress.c).  While a stress
 * command holds it, the UI renderer below skips lv_timer_handler() so the
 * LVGL heap is never touched from two threads at once. */
extern struct k_sem g_lvgl_sem;

/* LVGL display pump period. */
#define LVGL_TICK_MS 5

/* Heartbeat period (board user LED toggles at this rate). */
#define HEARTBEAT_MS 500

/* Runtime UI (uptime) refresh period. */
#define UI_REFRESH_MS 200

/* OLED backlight: gpio-leds node "oled_bl" (PG12, active high). */
static const struct device *oled_bl_dev = DEVICE_DT_GET(DT_NODELABEL(oled_bl));

/* Board user LED: gpio-leds node "leds", green_led at index 0. */
static const struct device *led_dev = DEVICE_DT_GET(DT_NODELABEL(leds));

int main(void)
{
    uint32_t font_mask = 0U;
    uint32_t last_hb = 0U;
    uint32_t last_ui_refresh = 0U;
    bool hb_on = false;
    char lvgl_ver[16];

    /* 1. Backlight on. */
    if (device_is_ready(oled_bl_dev))
    {
        (void)led_on(oled_bl_dev, 0);
    }

    /* 1b. Turn the panel on: the st7789v init sequence never sends
     * DISPON (0x29) - the app must call display_blanking_off() or the
     * panel stays in sleep-in and the screen stays black. */
    {
        const struct device *disp_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

        if (device_is_ready(disp_dev))
        {
            (void)display_blanking_off(disp_dev);
        }
    }

    /* 1c. Custom shell prompt (serial console is up by the time main runs;
     * requires CONFIG_SHELL_PROMPT_CHANGE=y). */
    {
        const struct shell *sh = shell_backend_uart_get_ptr();

        if (sh != NULL)
        {
            (void)shell_prompt_change(sh, "h743> ");
        }
    }

    /* 2. Mount SD + open font files. */
    if (lcd_driver_font_init() == RT_OK)
    {
        font_mask = lcd_driver_font_status();
        printk("FONT: UNIGBK=%u GBK12=%u GBK16=%u GBK24=%u GBK32=%u\n",
               (font_mask & FONT_MASK_UNIGBK) ? 1U : 0U,
               (font_mask & FONT_MASK_GBK12)  ? 1U : 0U,
               (font_mask & FONT_MASK_GBK16)  ? 1U : 0U,
               (font_mask & FONT_MASK_GBK24)  ? 1U : 0U,
               (font_mask & FONT_MASK_GBK32)  ? 1U : 0U);
    }
    else
    {
        printk("FONT: SD mount failed - Chinese glyphs unavailable\n");
    }

    /* 3. Register SD-backed GBK fonts (after the .FON files are open). */
    lv_port_font_init();

    /* 4. Build UI: versions, core clock, status lines. */
    snprintf(lvgl_ver, sizeof(lvgl_ver), "%d.%d.%d",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    ui_show(KERNEL_VERSION_STRING, lvgl_ver,
            (uint32_t)CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC);
    ui_set_font_status((font_mask != 0U) ? "字库 已加载" : "字库 未加载");

    printk("SYS: Zephyr %s, LVGL %s, core clock %u Hz\n",
           KERNEL_VERSION_STRING, lvgl_ver,
           (unsigned)CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC);

    /* 5. Display pump + heartbeat + periodic UI refresh. */
    while (1)
    {
        uint32_t now = k_uptime_get_32();

        /* Render only when no stress test owns the LVGL heap lock. */
        if (k_sem_take(&g_lvgl_sem, K_NO_WAIT) == 0)
        {
            lv_timer_handler();
            k_sem_give(&g_lvgl_sem);
        }

        /* Heartbeat LED toggles every HEARTBEAT_MS. */
        if (device_is_ready(led_dev) && (now - last_hb) >= HEARTBEAT_MS)
        {
            last_hb = now;
            hb_on = !hb_on;
            if (hb_on)
            {
                (void)led_on(led_dev, 0);
            }
            else
            {
                (void)led_off(led_dev, 0);
            }
            ui_set_led(hb_on);
        }

        /* Refresh the running-time line at a low rate. */
        if ((now - last_ui_refresh) >= UI_REFRESH_MS)
        {
            last_ui_refresh = now;
            ui_update_uptime(now);
        }

        k_sleep(K_MSEC(LVGL_TICK_MS));
    }
}
