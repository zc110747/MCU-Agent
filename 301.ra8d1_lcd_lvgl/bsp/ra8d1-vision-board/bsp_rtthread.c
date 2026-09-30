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

#include "bsp_api.h"
#include "bsp_pin.h"
#include "bsp_uart.h"

/* RT-Thread heap: a static array in .bss, so its cost shows up in size.txt
   instead of being hidden behind a linker-script reservation. */
#define RT_HEAP_SIZE    (64U * 1024U)

static rt_uint8_t g_rt_heap[RT_HEAP_SIZE] __attribute__((aligned(8)));
static bool       g_console_ready = false;

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
