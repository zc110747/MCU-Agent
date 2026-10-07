/**
 * @file main.c
 * @brief Application entry: RT-Thread Nano + LED + UART + MIPI DSI panel.
 *
 * Phase 0 skeleton: bring up the panel and a heartbeat, so the build/flash
 * chain can be validated before the NES core, W25Q64 and USB MSC land.
 *
 * NOTE: entry() is NOT here. For __GNUC__ RT-Thread provides it in
 * third_party/rt-thread-nano/src/components.c: entry() -> rtthread_startup(),
 * which creates the "main" thread that runs main().
 */
#include <rtthread.h>

#include <stddef.h>

#include "bsp_api.h"
#include "bsp_gpu.h"
#include "bsp_led.h"
#include "bsp_lcd.h"
#include "bsp_diag.h"
#include "bsp_ospi.h"
#include "bsp_rtthread.h"
#include "bsp_uart.h"
#include "nes.h"
#include "nes_heap.h"
#include "nes_port.h"
#include "rt_key_scan.h"

#define LED_THREAD_STACK_SIZE    (512U)
#define LED_THREAD_PRIORITY      (20U)
#define LED_BLINK_INTERVAL_MS    (500U)
#define HEARTBEAT_INTERVAL_MS    (2000U)

/* The heartbeat is diagnostic noise on the console: it shares SCI9 with the
   shell and, at 2 s intervals, collides with command echo often enough to make
   scripted command/response capture unreliable.  Default it off; `hb on`
   re-enables it when the boot log is what is being watched. */
static volatile bool g_heartbeat_enabled = false;

/* TEMP PROBE: main's own TCB address, captured at the top of main().  Read it
   over SWD to decide whether the main thread still exists once the console
   goes silent. */
uint32_t g_main_tcb_probe = 0U;
volatile uint32_t g_loop_lap = 0U;

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
        /* bare-hex convenience for the debug CLI: any a-f letter implies hex. */
        for (const char * p = s; '\0' != *p; p++)
        {
            if ((('a' <= *p) && (*p <= 'f')) || (('A' <= *p) && (*p <= 'F')))
            {
                base = 16;
                break;
            }
        }
    }

    while ('\0' != *s)
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

/* Heartbeat toggle: off by default so scripted captures are clean. */
static void hb_cmd (uint8_t argc, char ** argv)
{
    if (argc < 2U)
    {
        rt_kprintf("hb: %s\n", g_heartbeat_enabled ? "on" : "off");
        return;
    }

    if (0 == rt_strcmp(argv[1], "on"))
    {
        g_heartbeat_enabled = true;
    }
    else if (0 == rt_strcmp(argv[1], "off"))
    {
        g_heartbeat_enabled = false;
    }

    rt_kprintf("hb: %s\n", g_heartbeat_enabled ? "on" : "off");
}
MSH_CMD_EXPORT_ALIAS(hb_cmd, hb, heartbeat: hb on / off);

/* Thread state dump.
 *
 * Needed because `msh >` prints but the shell then never consumes input: the
 * RX ring fills and its tail does not move.  Neither the console lock nor the
 * OSPI init turned out to be responsible (both were A/B tested out), so the
 * next question is whether the tshell thread is blocked, dead, or absent.
 * This walks the kernel's own thread list rather than guessing from a
 * debugger. */
extern struct rt_object_information * rt_object_get_information(enum rt_object_class_type type);

static void dbg_cmd (uint8_t argc, char ** argv)
{
    if ((argc < 2U) || (0 != rt_strcmp(argv[1], "threads")))
    {
        rt_kprintf("usage: dbg threads\n");
        return;
    }

    {
        struct rt_object_information * info =
            rt_object_get_information(RT_Object_Class_Thread);
        rt_list_t * node;

        if (RT_NULL == info)
        {
            rt_kprintf("dbg: no thread information\n");
            return;
        }

        for (node = info->object_list.next;
             node != &info->object_list;
             node = node->next)
        {
            struct rt_thread * th =
                (struct rt_thread *) ((rt_uint8_t *) node -
                                      offsetof(struct rt_thread, tlist));

            rt_kprintf("dbg: th %-8s prio=%u stat=0x%02x sp=%p stack=%u\n",
                       th->parent.name,
                       (unsigned int) th->current_priority,
                       (unsigned int) th->stat,
                       th->sp,
                       (unsigned int) th->stack_size);
        }
    }
}
MSH_CMD_EXPORT_ALIAS(dbg_cmd, dbg, debug: dbg threads);

/* DRW2D: bring the engine up and blit a generated test pattern through it, so
   the 2D pipeline can be validated before the NES core depends on it. */
static void gpu_fill_test_pattern (void)
{
    /* 240x240 source, matching the NES frame size, blitted centred in the
       480x360 panel - this is exactly the geometry nes_draw() will use. */
    enum { W = 240, H = 240 };
    static uint16_t src[W * H];

    for (uint32_t y = 0; y < H; y++)
    {
        for (uint32_t x = 0; x < W; x++)
        {
            uint16_t r = (uint16_t) ((x * 31U) / (W - 1U));
            uint16_t g = (uint16_t) ((y * 63U) / (H - 1U));
            uint16_t b = (uint16_t) (((x + y) * 31U) / (W + H - 2U));

            src[(y * W) + x] = (uint16_t) ((r << 11) | (g << 5) | b);
        }
    }

    (void) bsp_gpu_blit_array((BSP_LCD_WIDTH - W) / 2U, (BSP_LCD_HEIGHT - H) / 2U,
                              src, W, H);
}

static void gpu_cmd (uint8_t argc, char ** argv)
{
    if ((argc < 2U) || (RT_NULL == argv[1]))
    {
        rt_kprintf("usage: gpu <init|info|fill HEX|blit>\n");
        return;
    }

    if (0 == rt_strcmp(argv[1], "init"))
    {
        fsp_err_t err = bsp_gpu_init();

        rt_kprintf("gpu init: %s (0x%x) last_err=%ld\n",
                   (FSP_SUCCESS == err) ? "OK" : "FAILED",
                   (unsigned int) err, (long) bsp_gpu_last_error());
    }
    else if (0 == rt_strcmp(argv[1], "info"))
    {
        rt_kprintf("gpu: %s blits=%u target=0x%08x %ux%u\n",
                   bsp_gpu_ready() ? "ready" : "down",
                   (unsigned int) bsp_gpu_blit_count(),
                   (unsigned int) (uint32_t) bsp_lcd_framebuffer(),
                   (unsigned int) BSP_LCD_WIDTH,
                   (unsigned int) BSP_LCD_HEIGHT);
    }
    else if (0 == rt_strcmp(argv[1], "fill"))
    {
        uint32_t color = (argc >= 3U) ? cmd_parse_u32(argv[2]) : 0U;

        (void) bsp_gpu_fill_rect(0, 0, BSP_LCD_WIDTH, BSP_LCD_HEIGHT, (uint16_t) color);
        rt_kprintf("gpu fill 0x%04x\n", (unsigned int) (uint16_t) color);
    }
    else if (0 == rt_strcmp(argv[1], "blit"))
    {
        if (!bsp_gpu_ready())
        {
            rt_kprintf("gpu blit: engine down (run 'gpu init')\n");
            return;
        }

        gpu_fill_test_pattern();
        rt_kprintf("gpu blit: 240x240 gradient centred, blits=%u\n",
                   (unsigned int) bsp_gpu_blit_count());
    }
    else
    {
        rt_kprintf("usage: gpu <init|info|fill HEX|blit>\n");
    }
}
MSH_CMD_EXPORT_ALIAS(gpu_cmd, gpu, DRW2D: gpu init / info / fill HEX / blit);

/* OSPI serial flash: the board's W25Q64-class NOR.  `ospi probe` is the first
   evidence gate of Phase 3 - it prints the JEDEC ID, which answers both "is the
   wiring right" and "what part is fitted". */
static void ospi_cmd (uint8_t argc, char ** argv)
{
    if ((argc < 2U) || (RT_NULL == argv[1]))
    {
        rt_kprintf("usage: ospi <init|id|bb|miso|pfs|cs|ints|regs|r ADDR LEN|w ADDR HEXBYTES|e ADDR|stat>\n");
        return;
    }

    if (0 == rt_strcmp(argv[1], "init"))
    {
        fsp_err_t err = bsp_ospi_init();

        rt_kprintf("ospi init: %s (0x%x)\n",
                   (FSP_SUCCESS == err) ? "OK" : "FAILED", (unsigned int) err);
    }
    else if (0 == rt_strcmp(argv[1], "bb"))
    {
        /* Physical-layer probe: drive the four SPI signals as plain GPIO and
           shift out 0x9F by hand.  Separates "controller misconfigured" from
           "no chip on the board". */
        fsp_err_t err = bsp_ospi_bitbang_id();
        const uint8_t * id = bsp_ospi_jedec_id();

        rt_kprintf("ospi bb: %s jdec=%02x %02x %02x capacity=%u bytes\n",
                   (FSP_SUCCESS == err) ? "ran" : "FAILED",
                   id[0], id[1], id[2], (unsigned int) bsp_ospi_capacity());
    }
    else if (0 == rt_strcmp(argv[1], "id"))
    {
        const uint8_t * id = bsp_ospi_jedec_id();

        rt_kprintf("ospi id: probed=%u jdec=%02x %02x %02x capacity=%u bytes\n",
                   (unsigned int) (bsp_ospi_probed() ? 1U : 0U),
                   id[0], id[1], id[2], (unsigned int) bsp_ospi_capacity());
    }
    else if (0 == rt_strcmp(argv[1], "stat"))
    {
        rt_kprintf("ospi stat: %s status=0x%02x\n",
                   bsp_ospi_ready() ? "ready" : "down",
                   (unsigned int) bsp_ospi_status());
    }
    else if (0 == rt_strcmp(argv[1], "r"))
    {
        uint32_t addr = (argc >= 3U) ? cmd_parse_u32(argv[2]) : 0U;
        uint32_t len  = (argc >= 4U) ? cmd_parse_u32(argv[3]) : 16U;
        uint8_t  buf[16];

        if (len > sizeof(buf))
        {
            len = sizeof(buf);
        }

        if (FSP_SUCCESS != bsp_ospi_read(addr, buf, len))
        {
            rt_kprintf("ospi r: read failed\n");
            return;
        }

        rt_kprintf("ospi r: %08x:", (unsigned int) addr);
        for (uint32_t i = 0U; i < len; i++)
        {
            rt_kprintf(" %02x", buf[i]);
        }
        rt_kprintf("\n");
    }
    else if (0 == rt_strcmp(argv[1], "e"))
    {
        uint32_t addr = (argc >= 3U) ? cmd_parse_u32(argv[2]) : 0U;
        fsp_err_t err = bsp_ospi_erase_sector(addr);

        rt_kprintf("ospi e: sector %08x %s (0x%x)\n",
                   (unsigned int) addr,
                   (FSP_SUCCESS == err) ? "erased" : "FAILED",
                   (unsigned int) err);
    }
    else if (0 == rt_strcmp(argv[1], "w"))
    {
        /* Single 8-byte aligned word write, for a quick end-to-end check of
           erase -> program -> read without a filesystem. */
        uint32_t addr = (argc >= 3U) ? cmd_parse_u32(argv[2]) : 0U;
        uint64_t value = (argc >= 4U) ? (uint64_t) cmd_parse_u32(argv[3]) : 0U;
        uint64_t pattern = value;

        if (FSP_SUCCESS != bsp_ospi_write(addr, &pattern, sizeof(pattern)))
        {
            rt_kprintf("ospi w: write failed\n");
            return;
        }

        rt_kprintf("ospi w: %08x <= %08x%08x\n",
                   (unsigned int) addr,
                   (unsigned int) (value >> 32), (unsigned int) (value & 0xFFFFFFFFU));
    }
    else if (0 == rt_strcmp(argv[1], "pfs"))
    {
        /* Read the pin mux back from inside the firmware.  This is the only
           trustworthy view: a debugger reading PmnPFS while the core runs (or
           is halted) shows whatever the session left behind, while these
           values are read by the code that just wrote them.  PSEL must be
           0x1C (IOPORT_PERIPHERAL_OSPI) and PMR must be 1 on all six. */
        bsp_ospi_pfs_dump();
    }
    else if (0 == rt_strcmp(argv[1], "regs"))
    {
        static const char * names[12] =
        {
            "WRAPCFG", "COMCFG", "BMCFGCH0", "LIOCFGCS1",
            "BMCTL0", "CMCTLCH0", "CDCTL0",
            "CMCFG1_0", "CMCFG1_1", "CMCFG1_2", "COMSTT", "MSTPCRB",
        };
        uint32_t v[12];

        bsp_ospi_reg_dump(v);

        for (uint32_t i = 0U; i < 12U; i++)
        {
            rt_kprintf("ospi regs: %-10s = 0x%08x\n", names[i], (unsigned int) v[i]);
        }
    }
    else if (0 == rt_strcmp(argv[1], "cs"))
    {
        /* Does the chip-select monitoring path move at all?  Reported as raw
           evidence - see bsp_ospi_cs_activity_probe() for why this does not
           claim to prove CS is or is not asserting. */
        bool     idle_high    = false;
        bool     ever_changed = false;
        uint32_t idle_sample  = 0U;
        uint32_t min_sample   = 0U;

        bsp_ospi_cs_activity_probe(&idle_high, &ever_changed,
                                   &idle_sample, &min_sample);

        rt_kprintf("ospi cs: idle=0x%08x min=0x%08x changed=%d (idle_high=%d)\n",
                   (unsigned int) idle_sample, (unsigned int) min_sample,
                   (int) ever_changed, (int) idle_high);

        if (!ever_changed)
        {
            rt_kprintf("ospi cs: COMSTT did not move across 64 commands -> it"
                       " does not track the bus; CS state NOT determinable"
                       " from firmware\n");
        }
        else
        {
            rt_kprintf("ospi cs: COMSTT tracked bus activity (values above)\n");
        }
    }
    else if (0 == rt_strcmp(argv[1], "ints"))
    {
        /* What did the controller actually observe on the bus for one 0x9F? */
        uint32_t v[4];

        bsp_ospi_cmd_int_status(v);

        rt_kprintf("ospi ints: INTS=0x%08x INTC=0x%08x COMSTT=0x%08x BMCTL0=0x%08x\n",
                   (unsigned int) v[0], (unsigned int) v[1],
                   (unsigned int) v[2], (unsigned int) v[3]);
        rt_kprintf("ospi ints: CMDCMP=%u ECSCS=%u INTCS=%u BUSERRCH0=%u BUSERRCH1=%u\n",
                   (unsigned int) ((v[0] >> 0) & 1U),
                   (unsigned int) ((v[0] >> 8) & 1U),
                   (unsigned int) ((v[0] >> 12) & 1U),
                   (unsigned int) ((v[0] >> 20) & 1U),
                   (unsigned int) ((v[0] >> 21) & 1U));

        if (0U == ((v[0] >> 8) & 1U))
        {
            rt_kprintf("ospi ints: no ECS event -> chip-select never asserted;"
                       " the frame ran without touching the device\n");
        }
        else
        {
            rt_kprintf("ospi ints: ECS event seen -> chip-select did assert\n");
        }
    }
    else if (0 == rt_strcmp(argv[1], "miso"))
    {
        /* Line-health check: is the MISO wire intact, and is anything driving
           it?  See bsp_ospi_miso_probe() for how to read the two flags. */
        bool float_high = false;
        bool idle_low   = false;

        bsp_ospi_miso_probe(&float_high, &idle_low);

        rt_kprintf("ospi miso: idle_low=%d pullup_high=%d\n",
                   (int) idle_low, (int) float_high);

        if (float_high && idle_low)
        {
            rt_kprintf("ospi miso: wire OK, nothing drives it -> flash absent,"
                       " unpowered, or CS never reaches it\n");
        }
        else if (!float_high)
        {
            rt_kprintf("ospi miso: held LOW even with pull-up -> short to GND"
                       " or a device stuck driving\n");
        }
        else
        {
            rt_kprintf("ospi miso: reads high without pull-up -> an external"
                       " pull-up is fitted and the line is idle\n");
        }
    }
    else
    {
        rt_kprintf("usage: ospi <init|id|regs|pfs|cs|ints|bb|miso|r ADDR LEN|w ADDR HEXBYTES|e ADDR|stat>\n");
    }
}
MSH_CMD_EXPORT_ALIAS(ospi_cmd, ospi, OSPI flash: ospi init / id / regs / pfs / cs / ints / bb / miso / stat / r ADDR LEN / e ADDR);

/* ------------------------------------------------------------------------ */
/* NES emulator: the core's nes_run() is an endless frame loop, so it lives on
   its own thread and is stopped by nes_deinit() (which sets nes_quit). */
/* ------------------------------------------------------------------------ */

/* nes_run() keeps the whole emulator on the stack of whoever calls it.  nes_t
   is allocated from SDRAM (nes_heap), so what lives here is the working set:
   frame scratch, the 256-entry opcode table lookup path and the PPU line
   renderers.  4 KB is the upstream project's figure and has proven enough;
   it is a static array, not a thread-created one, so the size is visible in
   size.txt. */
#define NES_THREAD_STACK_SIZE   (4096U)
#define NES_THREAD_PRIORITY     (15U)

/* The ROM is not in the filesystem until Phase 3 lands the W25Q64 FatFs, so the
   default path is a placeholder that fails cleanly and is overridden per run. */
#define NES_DEFAULT_ROM_PATH    "nes/SuperMary.nes"

static volatile rt_bool_t g_nes_quit_req = RT_FALSE;
static volatile rt_bool_t g_nes_running  = RT_FALSE;
static rt_thread_t        g_nes_thread   = RT_NULL;
static nes_t *            g_nes          = RT_NULL;
static char               g_nes_path[64];
static uint32_t           g_nes_frames_at_start = 0U;

static void nes_thread_entry (void * parameter)
{
    (void) parameter;

    g_nes_running = RT_TRUE;

    nes_run(g_nes);

    g_nes_running = RT_FALSE;
}

static void nes_report (void)
{
    uint32_t total = 0U, used = 0U, peak = 0U, nalloc = 0U, nfail = 0U;
    uint32_t w = 0U, h = 0U;

    nes_heap_info(&total, &used, &peak, &nalloc, &nfail);
    nes_port_last_draw_size(&w, &h);

    rt_kprintf("nes: %s rom=%s\n",
               g_nes_running ? "running" : "idle",
               (0U != g_nes_path[0]) ? g_nes_path : "-");
    rt_kprintf("nes: draws=%u failed=%u last=%ux%u vsync=%u\n",
               (unsigned int) nes_port_draw_calls(),
               (unsigned int) nes_port_draw_failed(),
               (unsigned int) w, (unsigned int) h,
               (unsigned int) bsp_lcd_vsync_count());
    rt_kprintf("nes: heap total=%u used=%u peak=%u nalloc=%u nfail=%u\n",
               (unsigned int) total, (unsigned int) used,
               (unsigned int) peak, (unsigned int) nalloc,
               (unsigned int) nfail);

    {
        uint32_t cpf = nes_port_draw_cycles_per_frame();
        uint32_t emu = nes_port_emu_cycles_per_frame();

        /* CPU is 480 MHz, so one 60 Hz frame budget is 8,000,000 cycles. */
        rt_kprintf("nes: present=%u cyc/frame over %u frames (%u%% of budget)\n",
                   (unsigned int) cpf,
                   (unsigned int) nes_port_draw_frames(),
                   (unsigned int) ((cpf * 100U) / 8000000U));
        rt_kprintf("nes: emu=%u cyc/frame max %u (%u%% of budget), sleep=%u cyc\n",
                   (unsigned int) emu,
                   (unsigned int) nes_port_emu_max_cycles(),
                   (unsigned int) ((emu * 100U) / 8000000U),
                   (unsigned int) nes_port_sleep_cycles());
        rt_kprintf("nes: frame period=%u cyc (max %u) -> %u fps\n",
                   (unsigned int) nes_port_frame_period_cycles(),
                   (unsigned int) nes_port_frame_period_max_cycles(),
                   (unsigned int) (480000000U /
                                   ((nes_port_frame_period_cycles() != 0U)
                                        ? nes_port_frame_period_cycles()
                                        : 8000000U)));
    }

    rt_kprintf("nes: pad=%s events=%u capture=%s\n",
               key_scan_name(key_scan()),
               (unsigned int) key_scan_count(),
               key_scan_capture_enabled() ? "on" : "off");
}

/**
 * @brief Start the emulator thread on whatever ROM g_nes currently points at.
 *
 * Shared by `nes run` (filesystem) and `nes runmem` (SWD-staged image).  Both
 * paths validate the same way; only how g_nes got populated differs.
 */
static void nes_start_run (void)
{
    if (g_nes_running)
    {
        rt_kprintf("nes run: already running\n");
        return;
    }

    if (RT_NULL == g_nes)
    {
        rt_kprintf("nes run: no ROM loaded (try 'nes load <path>')\n");
        return;
    }

    if (!bsp_gpu_ready())
    {
        rt_kprintf("nes run: DRW2D is down ('gpu init')\n");
        return;
    }

    g_nes_quit_req        = RT_FALSE;
    g_nes_frames_at_start = bsp_lcd_vsync_count();

    /* Fresh timing baselines so the figures reported after a run describe that
       run, not a mix of runs. */
    nes_port_draw_timing_reset();
    nes_port_emu_timing_reset();

    g_nes_thread = rt_thread_create("nes",
                                    nes_thread_entry,
                                    RT_NULL,
                                    NES_THREAD_STACK_SIZE,
                                    NES_THREAD_PRIORITY,
                                    10U);
    if (RT_NULL == g_nes_thread)
    {
        rt_kprintf("nes run: thread create failed\n");
        return;
    }

    (void) rt_thread_startup(g_nes_thread);
    rt_kprintf("nes run: started\n");
}

static void nes_cmd (uint8_t argc, char ** argv)
{
    if ((argc < 2U) || (RT_NULL == argv[1]))
    {
        rt_kprintf("usage: nes <load PATH|runmem|run|stop|info|bench [S]|stage|keys on|off|key MASK|test [N]>\n");
        return;
    }

    if (0 == rt_strcmp(argv[1], "load"))
    {
        if (argc < 3U)
        {
            rt_kprintf("usage: nes load <path>\n");
            return;
        }

        if (g_nes_running)
        {
            rt_kprintf("nes load: stop the emulator first ('nes stop')\n");
            return;
        }

        /* Reuse the same SDRAM pool for every load; a previous session's
           allocations are released by nes_unload_file(). */
        if (RT_NULL != g_nes)
        {
            g_nes = RT_NULL;
        }

        g_nes = nes_load_file(argv[2]);
        if (RT_NULL == g_nes)
        {
            rt_kprintf("nes load: failed (%s)\n", argv[2]);
            g_nes_path[0] = '\0';
            return;
        }

        rt_strncpy(g_nes_path, argv[2], sizeof(g_nes_path) - 1U);
        g_nes_path[sizeof(g_nes_path) - 1U] = '\0';

        rt_kprintf("nes load: prg=%u*16k chr=%u*8k mapper=%u mirror=%u\n",
                   (unsigned int) g_nes->nes_rom.prg_rom_size,
                   (unsigned int) g_nes->nes_rom.chr_rom_size,
                   (unsigned int) g_nes->nes_rom.mapper_number,
                   (unsigned int) g_nes->nes_rom.mirroring_type);
    }
    else if (0 == rt_strcmp(argv[1], "runmem"))
    {
        /* Load a ROM that a host-side harness wrote into the staging window
           over SWD.  This is how the emulator is exercised before Phase 3 lands
           the filesystem; in normal operation `nes load <path>` is the entry
           point. */
        uint32_t  stage_size = 0U;
        uint8_t * stage      = nes_heap_staging(&stage_size);

        if (RT_NULL == stage)
        {
            rt_kprintf("nes runmem: no staging window (heap not ready?)\n");
            return;
        }

        /* First four bytes must be the iNES magic; a blank staging window means
           the harness did not write anything. */
        if ((0x4EU != stage[0]) || (0x45U != stage[1]) ||
            (0x53U != stage[2]) || (0x1AU != stage[3]))
        {
            rt_kprintf("nes runmem: staging window is not a ROM (magic %02x %02x %02x %02x, %u bytes reserved)\n",
                       stage[0], stage[1], stage[2], stage[3],
                       (unsigned int) stage_size);
            return;
        }

        if (g_nes_running)
        {
            rt_kprintf("nes runmem: already running\n");
            return;
        }

        g_nes = nes_load_rom(stage);
        if (RT_NULL == g_nes)
        {
            rt_kprintf("nes runmem: nes_load_rom failed\n");
            return;
        }

        rt_strncpy(g_nes_path, "<staging>", sizeof(g_nes_path) - 1U);
        g_nes_path[sizeof(g_nes_path) - 1U] = '\0';

        rt_kprintf("nes runmem: prg=%u*16k chr=%u*8k mapper=%u mirror=%u\n",
                   (unsigned int) g_nes->nes_rom.prg_rom_size,
                   (unsigned int) g_nes->nes_rom.chr_rom_size,
                   (unsigned int) g_nes->nes_rom.mapper_number,
                   (unsigned int) g_nes->nes_rom.mirroring_type);

        nes_start_run();
    }
    else if (0 == rt_strcmp(argv[1], "run"))
    {
        nes_start_run();
    }
    else if (0 == rt_strcmp(argv[1], "stop"))
    {
        if (!g_nes_running)
        {
            rt_kprintf("nes stop: not running\n");
            return;
        }

        /* nes_deinit() sets nes_quit; the loop then exits on its own and the
           thread function returns, which frees the thread's own resources. */
        (void) nes_deinit(g_nes);
        g_nes_quit_req = RT_TRUE;

        {
            uint32_t spins = 0U;

            while (g_nes_running && (spins < 2000U))
            {
                rt_thread_mdelay(1);
                spins++;
            }
        }

        rt_kprintf("nes stop: %s, frames=%u\n",
                   g_nes_running ? "still running" : "stopped",
                   (unsigned int) (bsp_lcd_vsync_count() - g_nes_frames_at_start));
    }
    else if (0 == rt_strcmp(argv[1], "stage"))
    {
        /* Print the ROM staging window so a host harness knows where to write.
           Parsed by tools/verify/verify_nes_rom.py. */
        uint32_t  size = 0U;
        uint8_t * p    = nes_heap_staging(&size);

        if (RT_NULL == p)
        {
            rt_kprintf("nes stage: none\n");
            return;
        }

        rt_kprintf("nes stage: addr=0x%08x size=%u\n",
                   (unsigned int) (uint32_t) (uintptr_t) p, (unsigned int) size);
    }
    else if (0 == rt_strcmp(argv[1], "info"))
    {
        nes_report();
    }
    else if (0 == rt_strcmp(argv[1], "bench"))
    {
        /* Steady-state re-measurement.
           `nes info` reports an average accumulated since the session started,
           which includes the startup transient - ROM load, PPU warm-up, the
           first frames' mapper setup.  That transient dominated the very first
           reading (25.2 M cyc/frame, 315%) and masked the real steady-state
           cost (7.9 M, 99%).  This command discards the history, lets the
           emulator settle, then reports what it actually costs per frame. */
        uint32_t settle_s = (argc >= 3U) ? cmd_parse_u32(argv[2]) : 3U;

        if (settle_s < 1U)  { settle_s = 1U; }
        if (settle_s > 30U) { settle_s = 30U; }

        if (!g_nes_running)
        {
            rt_kprintf("nes bench: not running\n");
            return;
        }

        nes_port_draw_timing_reset();
        nes_port_emu_timing_reset();

        rt_kprintf("nes bench: settling for %u s...\n", (unsigned int) settle_s);
        rt_thread_mdelay(settle_s * 1000U);

        nes_report();
    }
    else if (0 == rt_strcmp(argv[1], "keys"))
    {
        if (argc < 3U)
        {
            rt_kprintf("usage: nes keys <on|off>\n");
            return;
        }

        if (0 == rt_strcmp(argv[2], "on"))
        {
            key_scan_capture(RT_TRUE);
            rt_kprintf("nes keys: capture on (w/s/a/d move, j/k fire, m start, n select, space release)\n");
        }
        else
        {
            key_scan_capture(RT_FALSE);
            rt_kprintf("nes keys: capture off\n");
        }
    }
    else if (0 == rt_strcmp(argv[1], "key"))
    {
        /* Inject a pad state directly.  This is the hook the acceptance script
           uses: it proves the whole input path (mask -> translate -> joypad
           register -> emulator) without needing a human at the keyboard. */
        uint32_t mask = (argc >= 3U) ? cmd_parse_u32(argv[2]) : 0U;

        key_scan_set(mask);
        rt_kprintf("nes key: mask=0x%02x (%s) joypad=0x%04x\n",
                   (unsigned int) mask, key_scan_name(mask),
                   (unsigned int) key_scan_to_nes_joypad(mask));
    }
    else if (0 == rt_strcmp(argv[1], "test"))
    {
        /* Render synthetic frames through the real present path, with no ROM and
           no emulator thread.  Exercises nes_draw() -> bsp_gpu_blit_array() end
           to end so the render path can be verified independently of ROM
           availability (which needs the Phase 3 filesystem).

           Optional count lets the timing figure be averaged over several frames
           rather than being dominated by the first-frame cache warm-up:
               nes test       -> 1 frame
               nes test 120   -> 120 frames (~2 seconds of emulated output) */
        uint32_t reps = (argc >= 3U) ? cmd_parse_u32(argv[2]) : 1U;

        if (0U == reps) { reps = 1U; }
        if (reps > 600U) { reps = 600U; }

        nes_port_draw_timing_reset();
        for (uint32_t i = 0U; i < reps; i++)
        {
            nes_port_run_test_pattern();
        }

        rt_kprintf("nes test: %u frame(s) blitted, draws=%u failed=%u\n",
                   (unsigned int) reps,
                   (unsigned int) nes_port_draw_calls(),
                   (unsigned int) nes_port_draw_failed());
    }
    else
    {
        rt_kprintf("usage: nes <load PATH|runmem|run|stop|info|bench [S]|stage|keys on|off|key MASK|test [N]>\n");
    }
}
MSH_CMD_EXPORT_ALIAS(nes_cmd, nes, NES: nes load PATH / run / stop / info / keys on / key MASK / test);

int main (void)
{
    /* Serialise rt_kprintf before anything else can print concurrently.  The
       heartbeat thread and the msh shell both write to SCI9; upstream's
       rt_kprintf shares one static buffer with no lock, so overlapping calls
       corrupt each other's output.  See bsp_rtthread.c. */
    bsp_console_lock_init();
    bsp_diag_mark(10U);   /* console lock ready */

    /* TEMP PROBE: record main's own TCB so a host-side SWD read can tell
       whether the thread still exists after the system goes quiet. */
    g_main_tcb_probe = (uint32_t) (uintptr_t) rt_thread_self();

    rt_kprintf("\nRA8D1 Vision Board - Phase 2 (NES emulator core)\n");
    rt_kprintf("RT-Thread Nano %d.%d.%d, CPU %u Hz, tick %u Hz\n",
               RT_VERSION_MAJOR, RT_VERSION_MINOR, RT_VERSION_PATCH,
               SystemCoreClock, RT_TICK_PER_SECOND);

    bsp_diag_mark(11U);   /* creating led thread */
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
            bsp_lcd_fill(0x0000U); /* black */
        }
    }

    /* The 2D engine is the NES render path; bring it up right after the panel
       so a failure is visible in the boot log rather than at first frame. */
    if (g_lcd_started)
    {
        bsp_diag_mark(13U);   /* gpu init */
        fsp_err_t gpu_err = bsp_gpu_init();

        rt_kprintf("[main] DRW2D: %s (0x%x)\n",
                   (FSP_SUCCESS == gpu_err) ? "OK" : "FAILED",
                   (unsigned int) gpu_err);
    }

    /* Serial flash: identify it now so the boot log states which part is
       fitted.  A failure here is not fatal yet (nothing before Phase 3's
       filesystem depends on it), but it is exactly the line that says whether
       the OSPI wiring is right. */
    {
        bsp_diag_mark(14U);   /* ospi init */
        fsp_err_t ospi_err = bsp_ospi_init();

        if (FSP_SUCCESS == ospi_err)
        {
            const uint8_t * id = bsp_ospi_jedec_id();

            rt_kprintf("[main] OSPI flash: jdec=%02x %02x %02x, %u bytes\n",
                       id[0], id[1], id[2], (unsigned int) bsp_ospi_capacity());
        }
        else
        {
            /* Report the raw ID even on failure: an unrecognised part and a
               dead bus are very different faults and the ID tells them apart. */
            const uint8_t * id = bsp_ospi_jedec_id();

            rt_kprintf("[main] OSPI flash: FAILED (0x%x) jdec=%02x %02x %02x\n",
                       (unsigned int) ospi_err, id[0], id[1], id[2]);
        }
    }

    /* The emulator's working set (nes_t + ROM images) is far larger than the
       RT-Thread heap, so it gets its own bounded pool in SDRAM.  Init it before
       any `nes load` can be typed. */
    bsp_diag_mark(15U);   /* nes heap */
    nes_heap_init();
    nes_heap_staging_reserve();
    /* nes_t itself is the hot structure (2 KB cpu_ram touched on every opcode),
       so it is served from an on-chip SRAM arena instead - see nes_port.c. */
    nes_state_pool_init();

    /* DWT cycle counter: the present path's cost is measured, not assumed. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    {
        extern uint8_t __nes_heap_start[];
        uint32_t total = 0U, used = 0U, peak = 0U, nalloc = 0U, nfail = 0U;

        nes_heap_info(&total, &used, &peak, &nalloc, &nfail);
        rt_kprintf("[main] NES heap: %s %u bytes at 0x%08x (free %u)\n",
                   nes_heap_ready() ? "ready" : "FAILED",
                   (unsigned int) total,
                   (unsigned int) (uint32_t) (uintptr_t) __nes_heap_start,
                   (unsigned int) (total - used));
        rt_kprintf("[main] NES state arena: %s %u bytes (need %u)\n",
                   nes_state_pool_ready() ? "ready" : "FAILED",
                   (unsigned int) nes_state_pool_size(),
                   (unsigned int) sizeof(nes_t));
    }

    bsp_diag_mark(16U);   /* entering main loop */

    while (1)
    {
        rt_size_t total = 0;
        rt_size_t used  = 0;
        rt_size_t max   = 0;

        if (g_heartbeat_enabled)
        {
            rt_memory_info(&total, &used, &max);
            rt_kprintf("[heartbeat] tick=%u heap total=%u used=%u max=%u%s\n",
                       rt_tick_get(), (rt_uint32_t) total, (rt_uint32_t) used,
                       (rt_uint32_t) max, g_lcd_started ? "" : " lcd=down");
        }

        rt_thread_mdelay(HEARTBEAT_INTERVAL_MS);

        /* TEMP PROBE: bump a counter each lap so the host can tell "main is
           looping" from "main is wedged inside rt_thread_mdelay". */
        g_loop_lap++;
    }
}
