/**
 * @file main.c
 * @brief Phase 2 application: RT-Thread Nano + LED + UART + GLCDC test patterns
 *
 * NOTE: entry() is NOT here. For __GNUC__ RT-Thread provides it in
 * third_party/rt-thread-nano/src/components.c: entry() -> rtthread_startup(),
 * which creates the "main" thread that runs main().
 */
#include <rtthread.h>

#include "app_config.h"
#include "app_ui.h"
#include "bsp_api.h"
#include "bsp_led.h"
#include "bsp_lcd.h"
#include "lv_port.h"
#if APP_ENABLE_TOUCH
#include "bsp_touch.h"
#endif

#define LED_THREAD_STACK_SIZE    (512U)
#define LED_THREAD_PRIORITY      (20U)
#define LED_BLINK_INTERVAL_MS    (500U)
#define HEARTBEAT_INTERVAL_MS    (2000U)

static rt_thread_t g_led_thread  = RT_NULL;
static bool        g_lcd_started = false;

/* Set by "led on" / "led off": the blink thread then only holds the level
   instead of toggling, so the command result is observable (and stays put). */
static volatile bool g_led_manual = false;

/* Nano has no strtoul in kservice, so msh arguments are parsed here.
   Accepts decimal and 0x-prefixed hex. */
static uint32_t cmd_parse_u32 (const char * s)
{
    uint32_t value = 0;
    uint32_t base  = 10;

    if (RT_NULL == s)
    {
        return 0;
    }

    if (('0' == s[0]) && (('x' == s[1]) || ('X' == s[1])))
    {
        base = 16;
        s   += 2;
    }
    else
    {
        /* bare-hex convenience for the debug CLI: any a-f letter implies hex
           (e.g. "cam rd 3c 300a" must read 0x3c/0x300a, not decimal). */
        for (const char * p = s; '\0' != *p; p++)
        {
            if ((('a' <= *p) && (*p <= 'f')) || (('A' <= *p) && (*p <= 'F')))
            {
                base = 16;
                break;
            }
        }
    }

    while (('\0' != *s) && ('\0' != s[0]))
    {
        uint32_t digit;

        if ((*s >= '0') && (*s <= '9'))
        {
            digit = (uint32_t) (*s - '0');
        }
        else if ((base == 16U) && (*s >= 'a') && (*s <= 'f'))
        {
            digit = (uint32_t) (*s - 'a') + 10U;
        }
        else if ((base == 16U) && (*s >= 'A') && (*s <= 'F'))
        {
            digit = (uint32_t) (*s - 'A') + 10U;
        }
        else
        {
            break;
        }

        value = (value * base) + digit;
        s++;
    }

    return value;
}

static void led_thread_entry (void * parameter)
{
    (void) parameter;

    bsp_led_init();

    while (1)
    {
        if (!g_led_manual)
        {
            bsp_led_toggle();
        }

        rt_thread_mdelay(LED_BLINK_INTERVAL_MS);
    }
}

static void led_cmd (uint8_t argc, char ** argv)
{
    if ((argc < 2U) || (RT_NULL == argv[1]))
    {
        rt_kprintf("usage: led <on|off|blink>\n");
        return;
    }

    if (0 == rt_strcmp(argv[1], "on"))
    {
        g_led_manual = true;
        bsp_led_write(true);
        rt_kprintf("led on\n");
    }
    else if (0 == rt_strcmp(argv[1], "off"))
    {
        g_led_manual = true;
        bsp_led_write(false);
        rt_kprintf("led off\n");
    }
    else if (0 == rt_strcmp(argv[1], "blink"))
    {
        g_led_manual = false;
        rt_kprintf("led blink\n");
    }
    else
    {
        rt_kprintf("usage: led <on|off|blink>\n");
    }
}
/* MSH_CMD_EXPORT would register the command as "led_cmd" (the C symbol name);
   MSH_CMD_EXPORT_ALIAS keeps the msh name short: "led on" / "led off". */
MSH_CMD_EXPORT_ALIAS(led_cmd, led, control on-board LED: led on / off / blink);

static void lcd_cmd (uint8_t argc, char ** argv)
{
    if ((argc < 2U) || (RT_NULL == argv[1]))
    {
        rt_kprintf("usage: lcd <init|info|stat|dsi|vsync|pattern N|fill HEX|bl on|off>\n");
        return;
    }

    if (0 == rt_strcmp(argv[1], "init"))
    {
        fsp_err_t err = bsp_lcd_init();

        g_lcd_started = (FSP_SUCCESS == err);
        rt_kprintf("lcd init: %s (0x%x)\n", g_lcd_started ? "OK" : "FAILED", (unsigned int) err);
    }
    else if (0 == rt_strcmp(argv[1], "stat"))
    {
        bsp_lcd_status_t st;

        bsp_lcd_status(&st);
        rt_kprintf("BG.EN=0x%08x (EN=%u VEN=%u)\n",
                   (unsigned int) st.bg_en,
                   (unsigned int) (st.bg_en & 1U),
                   (unsigned int) ((st.bg_en >> 8) & 1U));
        rt_kprintf("BG.HSIZE=%u BG.VSIZE=%u\n",
                   (unsigned int) st.bg_hsize, (unsigned int) st.bg_vsize);
        rt_kprintf("STMON=0x%08x (L1UNDF=%u L2UNDF=%u)\n",
                   (unsigned int) st.stmon,
                   (unsigned int) ((st.stmon >> 1) & 1U),
                   (unsigned int) ((st.stmon >> 2) & 1U));
        rt_kprintf("PANEL_CLK=0x%08x (DCDR=%u CLKEN=%u CLKSEL=%u)\n",
                   (unsigned int) st.panel_clk,
                   (unsigned int) (st.panel_clk & 0x3FU),
                   (unsigned int) ((st.panel_clk >> 6) & 1U),
                   (unsigned int) ((st.panel_clk >> 8) & 1U));
    }
    else if (0 == rt_strcmp(argv[1], "dsi"))
    {
        bsp_lcd_dsi_status_t ds;

        bsp_lcd_dsi_status(&ds);
        rt_kprintf("DSI cmds=%u seq0=%u phy_status=0x%x\n",
                   (unsigned int) ds.cmd_count,
                   (unsigned int) ds.seq0_count,
                   (unsigned int) ds.phy_status);
        rt_kprintf("DSI link_status=0x%04x (CH0=%u CH1=%u VIDEO=%u)\n",
                   (unsigned int) ds.link_status,
                   (unsigned int) (ds.link_status & 0x1U),
                   (unsigned int) ((ds.link_status >> 4) & 0x1U),
                   (unsigned int) ((ds.link_status >> 8) & 0x1U));
        rt_kprintf("DSI ack_err=0x%08x vsync=%u\n",
                   (unsigned int) ds.ack_err,
                   (unsigned int) bsp_lcd_vsync_count());
    }
    else if (0 == rt_strcmp(argv[1], "vsync"))
    {
        rt_kprintf("lcd vsync: count=%u\n", (unsigned int) bsp_lcd_vsync_count());
    }
    else if (0 == rt_strcmp(argv[1], "info"))
    {
        rt_kprintf("lcd %ux%u bpp%u fb=0x%08x (%u bytes, .sdram)\n",
                   (unsigned int) BSP_LCD_WIDTH,
                   (unsigned int) BSP_LCD_HEIGHT,
                   (unsigned int) BSP_LCD_BPP,
                   (unsigned int) bsp_lcd_framebuffer(),
                   (unsigned int) BSP_LCD_FB_BYTES);
    }
    else if (0 == rt_strcmp(argv[1], "pattern"))
    {
        uint32_t index = (argc >= 3U) ? cmd_parse_u32(argv[2]) : 0U;

        bsp_lcd_pattern(index);
        rt_kprintf("lcd pattern %u\n", (unsigned int) index);
    }
    else if (0 == rt_strcmp(argv[1], "fill"))
    {
        uint32_t color = (argc >= 3U) ? cmd_parse_u32(argv[2]) : 0U;

        bsp_lcd_fill((uint16_t) color);
        rt_kprintf("lcd fill 0x%04x\n", (unsigned int) (uint16_t) color);
    }
    else if (0 == rt_strcmp(argv[1], "bl"))
    {
        bool on = (argc >= 3U) && (0 == rt_strcmp(argv[2], "on"));

        bsp_lcd_set_backlight(on);
        rt_kprintf("lcd backlight %s\n", on ? "on" : "off");
    }
    else
    {
        rt_kprintf("usage: lcd <init|info|stat|dsi|vsync|pattern N|fill HEX|bl on|off>\n");
    }
}
MSH_CMD_EXPORT_ALIAS(lcd_cmd, lcd, MIPI panel: lcd init / info / stat / dsi / vsync / pattern N / fill HEX);

static void lv_cmd (uint8_t argc, char ** argv)
{
    if ((argc < 2U) || (RT_NULL == argv[1]))
    {
        rt_kprintf("usage: lv <start|test|demo|info>\n");
        return;
    }

    if (0 == rt_strcmp(argv[1], "start"))
    {
        lv_port_start();
        rt_kprintf("lv start: %s\n", lv_port_running() ? "running" : "requested");
    }
    else if (0 == rt_strcmp(argv[1], "test"))
    {
        lv_port_test_screen();
        rt_kprintf("lv test: solid-rect screen requested\n");
    }
    else if (0 == rt_strcmp(argv[1], "demo"))
    {
        lv_port_demo_screen();
        rt_kprintf("lv demo: animated screen requested\n");
    }
    else if (0 == rt_strcmp(argv[1], "info"))
    {
        rt_kprintf("lv info: %s flushes=%u fps=%u mem=%u/%uKB (%u%%)\n",
                   lv_port_running() ? "running" : "down",
                   (unsigned int) lv_port_flush_count(),
                   (unsigned int) lv_port_fps(),
                   (unsigned int) lv_port_mem_used_kb(),
                   (unsigned int) (LV_PORT_MEM_TOTAL_KB),
                   (unsigned int) lv_port_mem_used_pct());
        rt_kprintf("lv loop=%u handler=%u\n",
                   (unsigned int) lv_port_loop_count(),
                   (unsigned int) lv_port_handler_count());
    }
    else
    {
        rt_kprintf("usage: lv <start|test|demo|info>\n");
    }
}
MSH_CMD_EXPORT_ALIAS(lv_cmd, lv, LVGL: lv start / test / demo / info);

/* No physical buttons on this board: the menu cursor is driven from the msh
   console.  Requests only set a volatile flag; the LVGL thread drains them in
   app_ui_service(), so no LVGL object is ever touched off-thread. */
static void menu_cmd (uint8_t argc, char ** argv)
{
    if ((argc < 2U) || (RT_NULL == argv[1]))
    {
        rt_kprintf("usage: menu <up|down|enter|back|select N|list>\n");
        return;
    }

    if (0 == rt_strcmp(argv[1], "up"))
    {
        app_ui_req_up();
        rt_kprintf("menu up -> %u\n", (unsigned int) app_ui_menu_index());
    }
    else if (0 == rt_strcmp(argv[1], "down"))
    {
        app_ui_req_down();
        rt_kprintf("menu down -> %u\n", (unsigned int) app_ui_menu_index());
    }
    else if (0 == rt_strcmp(argv[1], "enter"))
    {
        app_ui_req_enter();
        rt_kprintf("menu enter -> action %u\n",
                   (unsigned int) app_ui_menu_index());
    }
    else if (0 == rt_strcmp(argv[1], "back"))
    {
        app_ui_req_back();
        rt_kprintf("menu back\n");
    }
    else if (0 == rt_strcmp(argv[1], "select"))
    {
        uint32_t index = (argc >= 3U) ? cmd_parse_u32(argv[2]) : 0U;

        if (index >= (uint32_t) app_ui_menu_count())
        {
            rt_kprintf("menu select: out of range (0..%u)\n",
                       (unsigned int) (app_ui_menu_count() - 1U));
            return;
        }

        app_ui_req_select((uint8_t) index);
        rt_kprintf("menu select %u (%s)\n", (unsigned int) index,
                   app_ui_menu_label((uint8_t) index));
    }
    else if (0 == rt_strcmp(argv[1], "list"))
    {
        for (uint8_t i = 0U; i < app_ui_menu_count(); i++)
        {
            rt_kprintf("menu [%u] %s%s\n", (unsigned int) i,
                       (i == app_ui_menu_index()) ? "* " : "  ",
                       app_ui_menu_label(i));
        }
        rt_kprintf("menu page=%u action=%u\n",
                   (unsigned int) app_ui_current(),
                   (unsigned int) app_ui_last_action());
    }
    else
    {
        rt_kprintf("usage: menu <up|down|enter|back|select N|list>\n");
    }
}
MSH_CMD_EXPORT_ALIAS(menu_cmd, menu, UI menu: menu up / down / enter / back / select N / list);

#if APP_ENABLE_TOUCH
static void touch_cmd (uint8_t argc, char ** argv)
{
    if ((argc >= 2U) && (0 == rt_strcmp(argv[1], "read")))
    {
        bsp_touch_point_t point = {0};

        if (FSP_SUCCESS != bsp_touch_read(&point))
        {
            rt_kprintf("touch read: no answer (probe failed?)\n");
            return;
        }

        rt_kprintf("touch %s x=%u y=%u (panel %ux%u)\n",
                   point.pressed ? "DOWN" : "up  ",
                   (unsigned int) point.x, (unsigned int) point.y,
                   (unsigned int) BSP_TOUCH_WIDTH, (unsigned int) BSP_TOUCH_HEIGHT);
        return;
    }

    if ((argc >= 2U) && (0 == rt_strcmp(argv[1], "init")))
    {
        fsp_err_t err = bsp_touch_init();

        rt_kprintf("touch init: %s (0x%x)\n",
                   (FSP_SUCCESS == err) ? "OK" : "FAILED", (unsigned int) err);
        return;
    }

    if ((argc >= 2U) && (0 == rt_strcmp(argv[1], "scan")))
    {
        uint8_t addr;
        int     found = 0;

        rt_kprintf("touch scan: probing 0x08..0x77 ...\n");
        for (addr = 0x08U; addr <= 0x77U; addr++)
        {
            if (FSP_SUCCESS == bsp_touch_probe_addr(addr))
            {
                rt_kprintf("  ACK at 0x%02x\n", (unsigned int) addr);
                found++;
            }
        }
        rt_kprintf("touch scan: %d device(s)\n", found);
        return;
    }

    if ((argc >= 2U) && (0 == rt_strcmp(argv[1], "id")))
    {
        uint8_t fw = 0U;

        if (FSP_SUCCESS != bsp_touch_product_id(&fw, 1U))
        {
            rt_kprintf("touch id: no answer\n");
            return;
        }
        rt_kprintf("touch id: fw=0x%02x finger=0x%02x\n",
                   (unsigned int) fw, (unsigned int) bsp_touch_status_raw());
        return;
    }

    rt_kprintf("touch: %s (CST812T @0x15, SCI3 P408/P409) indev=%s\n",
               bsp_touch_ready() ? "ready" : "down",
               lv_port_touch_active() ? "registered" : "none");
    rt_kprintf("usage: touch <info|init|read|scan|id>\n");
}
MSH_CMD_EXPORT_ALIAS(touch_cmd, touch, CST812T: touch info / touch init / touch read / touch scan / touch id);
#endif /* APP_ENABLE_TOUCH */

int main (void)
{
    rt_kprintf("\nRA8D1 Vision Board - Phase 5\n");
    rt_kprintf("RT-Thread Nano %d.%d.%d, CPU %u Hz, tick %u Hz\n",
               RT_VERSION_MAJOR, RT_VERSION_MINOR, RT_VERSION_PATCH,
               SystemCoreClock, RT_TICK_PER_SECOND);

    g_led_thread = rt_thread_create("led",
                                    led_thread_entry,
                                    RT_NULL,
                                    LED_THREAD_STACK_SIZE,
                                    LED_THREAD_PRIORITY,
                                    20U);
    if (RT_NULL != g_led_thread)
    {
        (void) rt_thread_startup(g_led_thread);
    }
    else
    {
        rt_kprintf("[main] ERROR: failed to create LED thread\n");
    }

    {
        fsp_err_t err = bsp_lcd_init();

        g_lcd_started = (FSP_SUCCESS == err);
        rt_kprintf("[main] MIPI panel %ux%u: %s (0x%x)\n",
                   (unsigned int) BSP_LCD_WIDTH,
                   (unsigned int) BSP_LCD_HEIGHT,
                   g_lcd_started ? "OK" : "FAILED",
                   (unsigned int) err);

        if (g_lcd_started)
        {
            bsp_lcd_pattern(2U);   /* colour bars until LVGL paints over them */
            lv_port_start();       /* Phase 5: LVGL demo thread                */
        }
    }

    while (1)
    {
        rt_size_t total = 0;
        rt_size_t used  = 0;
        rt_size_t max   = 0;

        rt_memory_info(&total, &used, &max);
        rt_kprintf("[heartbeat] tick=%u heap total=%u used=%u max=%u%s\n",
                   rt_tick_get(), (rt_uint32_t) total, (rt_uint32_t) used,
                   (rt_uint32_t) max, g_lcd_started ? "" : " lcd=down");
        rt_thread_mdelay(HEARTBEAT_INTERVAL_MS);
    }
}
