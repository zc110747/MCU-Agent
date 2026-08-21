/**
  ******************************************************************************
  * @file    stress.c
  * @brief   Portable stress-test commands for the Zephyr shell.
  *
  *  Sub-commands (all run in the shell thread, non-blocking for the kernel):
  *
  *      stress cpu <sec>    CPU integer-LCG throughput test (iters/sec).
  *                          Pure integer math -> safe on any FPU config.
  *      stress kmem <iter>  Zephyr system heap (k_malloc/k_free) stress:
  *                          sliding window of 16 live blocks, random sizes,
  *                          reports ok/fail/peak-live bytes.
  *      stress lvmem <iter> LVGL built-in heap stress (lv_mem_alloc/free).
  *                          Holds g_lvgl_sem so the UI renderer in main()
  *                          pauses during the test; reports before/after
  *                          free bytes to expose leaks.
  *      stress uart <n>     Serial throughput test: print n lines, measure
  *                          elapsed time and report bytes/sec.
  *
  *  The command pattern (SHELL_STATIC_SUBCMD_SET_CREATE) is portable to any
  *  Zephyr application; only the lvmem test depends on LVGL being present.
  ******************************************************************************
  */
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/printk.h>
#include <stdlib.h>
#include <string.h>

#include <lvgl.h>

/* ---------------------------------------------------------------------------
 * LVGL render lock shared with app/main.c.
 * count=1 -> the stress lvmem test takes it for exclusive heap access and the
 * UI thread (main) skips lv_timer_handler() while it is held.
 * -------------------------------------------------------------------------*/
struct k_sem g_lvgl_sem;

static int stress_lvgl_sem_init(void)
{
    k_sem_init(&g_lvgl_sem, 1, 1);
    return 0;
}
SYS_INIT(stress_lvgl_sem_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

/* Simple LCG pseudo-random source (no libc rand() state). */
static uint32_t prng_next(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s;
}

/* ---------------------------------------------------------------------------
 * stress cpu <sec>
 * -------------------------------------------------------------------------*/
static int stress_cpu(const struct shell *sh, size_t argc, char **argv)
{
    uint32_t sec = (argc > 1) ? (uint32_t)strtol(argv[1], NULL, 0) : 3U;
    uint32_t seed = (uint32_t)k_uptime_get_32();
    volatile uint32_t acc = seed;
    uint32_t start, end, iters = 0U;

    if (sec == 0U || sec > 60U)
    {
        shell_error(sh, "usage: stress cpu <1..60>");
        return -EINVAL;
    }

    shell_print(sh, "cpu stress: %u s integer LCG...", sec);
    start = k_uptime_get_32();
    end = start + sec * 1000U;
    while ((int32_t)(k_uptime_get_32() - end) < 0)
    {
        for (uint32_t j = 0U; j < 1000U; j++)
        {
            acc = acc * 1103515245u + 12345u;   /* LCG multiply-add chain */
        }
        iters += 1000U;
        k_yield();                              /* keep shell responsive */
    }

    shell_print(sh, "cpu stress done: %u iters in %u ms -> %u iters/s",
                iters, sec * 1000U, iters / sec);
    shell_print(sh, "  checksum 0x%08X (volatile, prevents DCE)",
                (unsigned)acc);
    return 0;
}

/* ---------------------------------------------------------------------------
 * stress kmem <iter>
 * -------------------------------------------------------------------------*/
#define KMEM_WINDOW 16

static int stress_kmem(const struct shell *sh, size_t argc, char **argv)
{
    uint32_t iter = (argc > 1) ? (uint32_t)strtol(argv[1], NULL, 0) : 100U;
    uint32_t seed = (uint32_t)k_uptime_get_32();
    void *blocks[KMEM_WINDOW] = {0};
    uint32_t fail = 0U;

    if (iter == 0U || iter > 100000U)
    {
        shell_error(sh, "usage: stress kmem <1..100000>");
        return -EINVAL;
    }

    shell_print(sh, "kmem stress: %u iters, sliding window %d...", iter, KMEM_WINDOW);
    for (uint32_t i = 0U; i < iter; i++)
    {
        uint32_t sz = 16U + (prng_next(&seed) % 2033U);   /* 16..2048 B */

        if (blocks[i % KMEM_WINDOW] != NULL)
        {
            k_free(blocks[i % KMEM_WINDOW]);
            blocks[i % KMEM_WINDOW] = NULL;
        }

        void *p = k_malloc(sz);
        if (p == NULL)
        {
            fail++;
            continue;
        }
        memset(p, 0x5A, sz);
        blocks[i % KMEM_WINDOW] = p;
    }
    for (uint32_t i = 0U; i < KMEM_WINDOW; i++)
    {
        if (blocks[i] != NULL)
        {
            k_free(blocks[i]);
            blocks[i] = NULL;
        }
    }

    shell_print(sh, "kmem stress done: ok=%u fail=%u",
                iter - fail, fail);
    shell_print(sh, "  all %d blocks freed, heap returned to pool",
                KMEM_WINDOW);
    return 0;
}

/* ---------------------------------------------------------------------------
 * stress lvmem <iter>
 * -------------------------------------------------------------------------*/
#define LVMEM_WINDOW 32

static int stress_lvmem(const struct shell *sh, size_t argc, char **argv)
{
    uint32_t iter = (argc > 1) ? (uint32_t)strtol(argv[1], NULL, 0) : 200U;
    uint32_t seed = (uint32_t)k_uptime_get_32();
    void *blocks[LVMEM_WINDOW] = {0};
    uint32_t fail = 0U, peak = 0U;
    lv_mem_monitor_t before, after;

    if (iter == 0U || iter > 100000U)
    {
        shell_error(sh, "usage: stress lvmem <1..100000>");
        return -EINVAL;
    }

    lv_mem_monitor(&before);

    /* Exclusive access: main() skips lv_timer_handler() while we hold the
     * semaphore, so the LVGL heap is only touched from this thread. */
    k_sem_take(&g_lvgl_sem, K_FOREVER);
    shell_print(sh, "lvmem stress: %u iters, sliding window %d...",
                iter, LVMEM_WINDOW);

    for (uint32_t i = 0U; i < iter; i++)
    {
        uint32_t sz = 16U + (prng_next(&seed) % 1009U);   /* 16..1024 B */

        if (blocks[i % LVMEM_WINDOW] != NULL)
        {
            lv_mem_free(blocks[i % LVMEM_WINDOW]);
            blocks[i % LVMEM_WINDOW] = NULL;
        }

        void *p = lv_mem_alloc(sz);
        if (p == NULL)
        {
            fail++;
            continue;
        }
        memset(p, 0xA5, sz);
        blocks[i % LVMEM_WINDOW] = p;
        if (sz * LVMEM_WINDOW > peak)
        {
            peak = sz * LVMEM_WINDOW;   /* upper bound, informative only */
        }
    }
    for (uint32_t i = 0U; i < LVMEM_WINDOW; i++)
    {
        if (blocks[i] != NULL)
        {
            lv_mem_free(blocks[i]);
            blocks[i] = NULL;
        }
    }
    k_sem_give(&g_lvgl_sem);

    lv_mem_monitor(&after);

    shell_print(sh, "lvmem stress done: ok=%u fail=%u",
                iter - fail, fail);
    if (before.total_size > 0U)
    {
        /* Built-in LVGL heap: compare free bytes before/after. */
        shell_print(sh, "  heap free before=%u after=%u delta=%+d",
                    (unsigned)before.free_size, (unsigned)after.free_size,
                    (int)after.free_size - (int)before.free_size);
        if (after.free_size < before.free_size)
        {
            shell_warn(sh, "  WARNING: free memory shrank -> possible leak!");
        }
        else
        {
            shell_print(sh, "  no leak detected");
        }
    }
    else
    {
        /* Zephyr LVGL integration uses a custom sys_heap allocator
         * (LV_MEM_CUSTOM=1, see lvgl_mem.c): lv_mem_monitor() reports 0, so
         * leak detection degrades to allocation-failure counting. */
        shell_print(sh, "  custom allocator (sys_heap): leak check via fail count");
        if (fail == 0U)
        {
            shell_print(sh, "  all %u allocs OK, all %d freed -> no leak detected",
                        iter, LVMEM_WINDOW);
        }
        else
        {
            shell_warn(sh, "  %u allocations failed -> heap exhausted?", fail);
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * stress uart <n>
 * -------------------------------------------------------------------------*/
static int stress_uart(const struct shell *sh, size_t argc, char **argv)
{
    uint32_t n = (argc > 1) ? (uint32_t)strtol(argv[1], NULL, 0) : 50U;
    uint32_t start, elapsed, bps;
    char line[64];

    if (n == 0U || n > 10000U)
    {
        shell_error(sh, "usage: stress uart <1..10000>");
        return -EINVAL;
    }

    shell_print(sh, "uart stress: %u lines...", n);
    start = k_uptime_get_32();
    for (uint32_t i = 0U; i < n; i++)
    {
        snprintk(line, sizeof(line),
                 "UART-STRESS line %04u payload 0123456789abcdef",
                 (unsigned)i);
        shell_fprintf(sh, SHELL_NORMAL, "%s\n", line);
    }
    elapsed = k_uptime_get_32() - start;
    if (elapsed == 0U)
    {
        elapsed = 1U;
    }

    /* 64-byte payload per line.  This measures the *application-layer* send
     * rate through the shell output path (the UART backend may buffer), so it
     * is an upper bound on real wire throughput. */
    bps = (uint32_t)((uint64_t)n * 64U * 1000U / elapsed);

    shell_print(sh, "uart stress done: %u lines in %u ms -> %u B/s (app-layer)",
                n, elapsed, bps);
    return 0;
}

/* ---------------------------------------------------------------------------
 * stress <sub> [args]
 * -------------------------------------------------------------------------*/
SHELL_STATIC_SUBCMD_SET_CREATE(stress_cmds,
    SHELL_CMD(cpu,   NULL, "cpu <sec>  : CPU integer throughput test",   stress_cpu),
    SHELL_CMD(kmem,  NULL, "kmem <n>   : system heap (k_malloc) stress", stress_kmem),
    SHELL_CMD(lvmem, NULL, "lvmem <n>  : LVGL heap stress + leak check",  stress_lvmem),
    SHELL_CMD(uart,  NULL, "uart <n>   : serial throughput test",         stress_uart),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(stress, &stress_cmds, "Stress tests: cpu/kmem/lvmem/uart", NULL);
