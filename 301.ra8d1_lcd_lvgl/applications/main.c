/**
 * @file main.c
 * @brief Phase 2 application: RT-Thread Nano + LED + UART + GLCDC test patterns
 *
 * NOTE: entry() is NOT here. For __GNUC__ RT-Thread provides it in
 * third_party/rt-thread-nano/src/components.c: entry() -> rtthread_startup(),
 * which creates the "main" thread that runs main().
 */
#include <rtthread.h>

#include "bsp_api.h"
#include "bsp_led.h"
#include "bsp_lcd.h"
#include "bsp_cam.h"
#include "bsp_sccb.h"

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
        rt_kprintf("usage: lcd <init|info|pattern N|fill HEX|bl on|off>\n");
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
        rt_kprintf("usage: lcd <init|info|pattern N|fill HEX|bl on|off>\n");
    }
}
MSH_CMD_EXPORT_ALIAS(lcd_cmd, lcd, RGB panel: lcd init / info / pattern N / fill HEX);

static void cam_cmd (uint8_t argc, char ** argv)
{
    if ((argc < 2U) || (RT_NULL == argv[1]))
    {
        rt_kprintf("usage: cam <init|scan|snap|stat|rd A R|bar on|off>\n");
        return;
    }

    if (0 == rt_strcmp(argv[1], "init"))
    {
        fsp_err_t err = bsp_cam_init();

        rt_kprintf("cam init: %s (0x%x)\n", (FSP_SUCCESS == err) ? "OK" : "FAILED",
                   (unsigned int) err);
    }
    else if (0 == rt_strcmp(argv[1], "scan"))
    {
        bsp_cam_scan_t scan = {0};
        fsp_err_t      err  = bsp_cam_scan(&scan);

        if (FSP_SUCCESS == err)
        {
            rt_kprintf("cam scan: ACK addr7=0x%02x id=0x%04x (%s)\n",
                       scan.addr7, scan.id, scan.name);
        }
        else
        {
            rt_kprintf("cam scan: no device ACKed (0x%x)\n", (unsigned int) err);
        }
    }
    else if (0 == rt_strcmp(argv[1], "snap"))
    {
        fsp_err_t err = bsp_cam_snap();

        if (FSP_SUCCESS == err)
        {
            uint8_t *  fb  = bsp_cam_framebuffer();
            uint32_t   sum = 0U;

            for (uint32_t i = 0U; i < BSP_CAM_FRAME_BYTES / 2U; i++)
            {
                sum += ((uint16_t) fb[2 * i] << 8) | fb[2 * i + 1U];
            }

            rt_kprintf("cam snap: OK fb=0x%08x first=%02x %02x %02x %02x %02x %02x %02x %02x sum=0x%08x\n",
                       (unsigned int) (uint32_t) fb,
                       fb[0], fb[1], fb[2], fb[3], fb[4], fb[5], fb[6], fb[7],
                       (unsigned int) sum);
        }
        else
        {
            rt_kprintf("cam snap: FAILED (0x%x)\n", (unsigned int) err);
        }
    }
    else if (0 == rt_strcmp(argv[1], "rd"))
    {
        /* cam rd <addr7hex> <reg16hex>: raw 16-bit-reg debug read. */
        if (argc >= 4U)
        {
            uint8_t  addr = (uint8_t) cmd_parse_u32(argv[2]);
            uint16_t reg  = (uint16_t) cmd_parse_u32(argv[3]);
            uint8_t  val  = 0U;
            fsp_err_t err = bsp_sccb_read16(addr, reg, &val);

            rt_kprintf("cam rd 0x%02x[0x%04x]: %s val=0x%02x (0x%x)\n",
                       addr, reg, (FSP_SUCCESS == err) ? "OK" : "FAIL",
                       val, (unsigned int) err);
        }
        else
        {
            rt_kprintf("usage: cam rd <addr7hex> <reg16hex>  (e.g. cam rd 0x3c 0x300a)\n");
        }
    }
    else if (0 == rt_strcmp(argv[1], "stat"))
    {
        rt_kprintf("cam %s, %ux%u bpp%u fb=0x%08x (%u bytes, .nocache_sdram)\n",
                   bsp_cam_ready() ? "ready" : "down",
                   (unsigned int) BSP_CAM_WIDTH,
                   (unsigned int) BSP_CAM_HEIGHT,
                   (unsigned int) BSP_CAM_BPP,
                   (unsigned int) (uint32_t) bsp_cam_framebuffer(),
                   (unsigned int) BSP_CAM_FRAME_BYTES);
    }
    else if (0 == rt_strcmp(argv[1], "bar"))
    {
        bool on  = (argc >= 3U) && (0 == rt_strcmp(argv[2], "on"));
        fsp_err_t err = bsp_cam_colorbar(on);

        rt_kprintf("cam colorbar %s: %s (0x%x)\n", on ? "on" : "off",
                   (FSP_SUCCESS == err) ? "OK" : "FAILED", (unsigned int) err);
    }
    else
    {
        rt_kprintf("usage: cam <init|scan|snap|stat|rd A R|bar on|off>\n");
    }
}
MSH_CMD_EXPORT_ALIAS(cam_cmd, cam, camera: cam init / scan / snap / stat / rd A R / bar on|off);

int main (void)
{
    rt_kprintf("\nRA8D1 Vision Board - Phase 3\n");
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
        rt_kprintf("[main] GLCDC %ux%u: %s (0x%x)\n",
                   (unsigned int) BSP_LCD_WIDTH,
                   (unsigned int) BSP_LCD_HEIGHT,
                   g_lcd_started ? "OK" : "FAILED",
                   (unsigned int) err);

        if (g_lcd_started)
        {
            bsp_lcd_pattern(2U);   /* colour bars: the easiest thing to eyeball */
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
