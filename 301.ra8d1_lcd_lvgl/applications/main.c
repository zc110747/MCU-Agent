/**
 * @file main.c
 * @brief Phase 1 application: RT-Thread Nano + LED thread + heartbeat
 *
 * NOTE: entry() is NOT here. For __GNUC__ RT-Thread provides it in
 * third_party/rt-thread-nano/src/components.c: entry() -> rtthread_startup(),
 * which creates the "main" thread that runs main().
 */
#include <rtthread.h>

#include "bsp_api.h"
#include "bsp_led.h"

#define LED_THREAD_STACK_SIZE    (512U)
#define LED_THREAD_PRIORITY      (20U)
#define LED_BLINK_INTERVAL_MS    (500U)
#define HEARTBEAT_INTERVAL_MS    (2000U)

static rt_thread_t g_led_thread  = RT_NULL;

/* Set by "led on" / "led off": the blink thread then only holds the level
   instead of toggling, so the command result is observable (and stays put). */
static volatile bool g_led_manual = false;

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

int main (void)
{
    rt_kprintf("\nRA8D1 Vision Board - Phase 1\n");
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

    while (1)
    {
        rt_size_t total = 0;
        rt_size_t used  = 0;
        rt_size_t max   = 0;

        rt_memory_info(&total, &used, &max);
        rt_kprintf("[heartbeat] tick=%u heap total=%u used=%u max=%u\n",
                   rt_tick_get(), (rt_uint32_t) total, (rt_uint32_t) used, (rt_uint32_t) max);
        rt_thread_mdelay(HEARTBEAT_INTERVAL_MS);
    }
}
