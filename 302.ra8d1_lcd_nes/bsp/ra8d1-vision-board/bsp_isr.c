/**
 * @file bsp_isr.c
 * @brief RT-Thread context wrappers around the FSP interrupt handlers.
 *
 * FSP's ISRs are written to be RTOS-agnostic and call FSP_CONTEXT_SAVE /
 * FSP_CONTEXT_RESTORE at entry and exit.  Those expand to nothing unless
 * BSP_CFG_RTOS selects ThreadX (see bsp_common.h), and this project is built
 * with BSP_CFG_RTOS=2 meaning "some other RTOS" - so an FSP ISR never tells
 * the kernel that it is running in interrupt context.
 *
 * That breaks RT-Thread in a specific and very confusing way.  Kernel calls
 * like rt_sem_release() end with:
 *
 *     if (rt_interrupt_get_nest() == 0)
 *         rt_schedule();                 // reschedule right here
 *     else
 *         rt_thread_switch_interrupt_flag = 1;   // reschedule on ISR exit
 *
 * With the nesting count stuck at 0, an ISR that posts a semaphore takes the
 * first branch and calls rt_schedule() from inside the interrupt.  The waiting
 * thread is marked ready but never actually resumes - the CPU returns to
 * whatever it interrupted and eventually falls into the idle thread.  From the
 * outside the symptom is "the shell went deaf": characters arrive (the ring
 * fills, the semaphore even counts up) but the msh thread never runs again.
 *
 * The wrappers below put the enter/leave pair around the FSP handlers, so the
 * kernel sees the correct context and defers the switch to ISR exit, where the
 * port's PendSV path performs it properly.
 *
 * Only interrupts whose handlers can reach an RT-Thread API need wrapping:
 *   SCI9 RXI  - bsp_uart_callback() calls rt_sem_release()
 *   GLCDC VS  - the line-detect handler calls nes_port_vsync_notify()
 * The DSI and DRW handlers only touch hardware, so they are left alone.
 */
#include <rtthread.h>

/* The FSP handlers, defined inside the upstream driver .c files. */
void sci_b_uart_rxi_isr (void);
void glcdc_line_detect_isr (void);

/** SCI9 receive ISR: RT-Thread-aware wrapper. */
void bsp_uart_rxi_isr (void)
{
    rt_interrupt_enter();
    sci_b_uart_rxi_isr();
    rt_interrupt_leave();
}

/** GLCDC line-detect (vsync) ISR: RT-Thread-aware wrapper. */
void bsp_glcdc_line_isr (void)
{
    rt_interrupt_enter();
    glcdc_line_detect_isr();
    rt_interrupt_leave();
}
