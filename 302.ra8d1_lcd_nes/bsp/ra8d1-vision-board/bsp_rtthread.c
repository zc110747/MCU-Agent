/**
 * @file bsp_rtthread.c
 * @brief RT-Thread Nano board port for RA8D1 Vision Board (Cortex-M85)
 *
 * Provides the three symbols the kernel expects from the BSP:
 *   rt_hw_board_init()      - SysTick + heap + console
 *   rt_hw_console_output()  - rt_kprintf() sink (UART9, polled)
 *   rt_hw_console_getchar() - finsh/msh input source (UART9, interrupt + ring buffer)
 */
#include <rtthread.h>

#include <stdarg.h>

#include "bsp_api.h"
#include "bsp_diag.h"
#include "bsp_pin.h"
#include "bsp_uart.h"

/* RT-Thread heap: a static array in .bss, so its cost shows up in size.txt
   instead of being hidden behind a linker-script reservation. */
#define RT_HEAP_SIZE    (64U * 1024U)

static rt_uint8_t g_rt_heap[RT_HEAP_SIZE] __attribute__((aligned(8)));
static bool       g_console_ready = false;

/* ---------------------------------------------------------------------------
 * Locked rt_kprintf
 * ---------------------------------------------------------------------------
 * The Nano implementation (kservice.c) formats into a single static buffer:
 *
 *     static char rt_log_buf[RT_CONSOLEBUF_SIZE];
 *     length = rt_vsnprintf(rt_log_buf, ...);
 *     rt_hw_console_output(rt_log_buf);
 *
 * with no lock of any kind.  Two threads printing at once therefore corrupt
 * each other in two distinct ways: whichever formats second overwrites the
 * buffer the first is still transmitting, and the two rt_hw_console_output
 * calls interleave on the UART.
 *
 * The symptom is exactly what the heartbeat + shell pair produced: the shell
 * echo and the heartbeat line merge into noise ("msh >[heartbeat] tick0...")
 * and lines stop mid-way ("[heartbeat] tick=" with the %u never emitted).
 * It is *not* an optimiser problem - the earlier -O1/-O2 note was this bug
 * seen through a slower console.
 *
 * rt_kprintf is rt_weak, so overriding it here fixes the console for every
 * caller (shell, heartbeat, drivers) without forking anything upstream.  The
 * override keeps its own buffer on the stack of the caller and holds a lock
 * across format+transmit, which makes each line atomic.
 *
 * The lock is a binary semaphore rather than a mutex because this kernel is
 * built with RT_USING_SEMAPHORE only.  Priority inheritance would be nice in
 * principle, but every critical section here is one 256-byte polled UART write
 * (a few hundred microseconds), so inversion cannot be more than that.
 */
#define BSP_CONSOLE_BUF_SIZE    (256U)

static struct rt_semaphore g_console_lock;
static bool               g_console_lock_ready = false;

int rt_kprintf (const char * fmt, ...)
{
    char    buf[BSP_CONSOLE_BUF_SIZE];
    va_list args;
    int     length;

    if (!g_console_ready)
    {
        return 0;
    }

    va_start(args, fmt);
    length = rt_vsnprintf(buf, sizeof(buf) - 1U, fmt, args);
    va_end(args);

    if (length < 0)
    {
        return 0;
    }

    if ((rt_size_t) length > (sizeof(buf) - 1U))
    {
        length = (int) (sizeof(buf) - 1U);
    }

    /* Take the lock only when the kernel is far enough along to schedule.
       rt_kprintf runs during board init before the scheduler starts, and
       rt_sem_take() before that point would fault. */
    if (g_console_lock_ready)
    {
        (void) rt_sem_take(&g_console_lock, RT_WAITING_FOREVER);
    }

    bsp_uart_write(buf, (uint32_t) length);

    if (g_console_lock_ready)
    {
        (void) rt_sem_release(&g_console_lock);
    }

    return length;
}

/**
 * @brief Configure SysTick as the RT-Thread OS tick.
 */
void rt_hw_systick_init (void)
{
    SysTick_Config(SystemCoreClock / RT_TICK_PER_SECOND);
    NVIC_SetPriority(SysTick_IRQn, 0xFFU);
}

/**
 * @brief OS tick ISR.
 */
void SysTick_Handler (void)
{
    rt_interrupt_enter();
    rt_tick_increase();
    rt_interrupt_leave();

    /* Keep a liveness snapshot where a debugger can always read it, even if
       every thread is wedged and the console is silent.  Runs after
       rt_interrupt_leave() so the snapshot records the post-tick state. */
    bsp_diag_tick();
}

/**
 * @brief Board-level init, called at the top of rtthread_startup().
 */
void rt_hw_board_init (void)
{
    rt_hw_systick_init();

    rt_system_heap_init((void *) &g_rt_heap[0], (void *) &g_rt_heap[RT_HEAP_SIZE]);

    /* Pin mux first: every FSP driver (SCI9, GLCDC, SDRAM bus) assumes its
       pins are already selected when Open() runs. */
    (void) bsp_pin_init();

    bsp_uart_init();
    g_console_ready = true;

#ifdef RT_USING_COMPONENTS_INIT
    rt_components_board_init();
#endif
}

/**
 * @brief Enable the console lock. Call from a thread once the kernel runs.
 *
 * Deliberately not part of rt_hw_board_init(): the scheduler is not started
 * at that point, and taking a mutex before then is undefined.
 */
void bsp_console_lock_init (void)
{
    if (g_console_lock_ready)
    {
        return;
    }

    if (RT_EOK == rt_sem_init(&g_console_lock, "kprintf", 1, RT_IPC_FLAG_PRIO))
    {
        g_console_lock_ready = true;
    }
}

/**
 * @brief rt_kprintf() sink.
 */
void rt_hw_console_output (const char * str)
{
    if (!g_console_ready)
    {
        return;
    }

    bsp_uart_write(str, (uint32_t) rt_strlen(str));
}

/**
 * @brief finsh/msh input source. Blocks until a character arrives.
 */
char rt_hw_console_getchar (void)
{
    return bsp_uart_getchar();
}
