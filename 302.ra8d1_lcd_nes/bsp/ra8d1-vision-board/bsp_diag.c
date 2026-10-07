/**
 * @file bsp_diag.c
 * @brief Crash-proof kernel liveness snapshot for host-side (SWD) inspection.
 *
 * The msh console dies partway through a line on this board, and a dead
 * console makes every "which thread is stuck where" question unanswerable
 * from the UART.  This module keeps an answer in a fixed, pinned RAM struct
 * that a debugger can read at any time - it does not need the console, the
 * scheduler, or any thread to cooperate.
 *
 * Design rules that make it useful when things are broken:
 *   - The struct is written from the SysTick handler, which provably runs
 *     as long as the CPU executes at all, even if every task is wedged.
 *   - Only plain volatile stores are used - no locks, no allocation, no
 *     rt_kprintf, so the snapshot can never be the thing that hangs.
 *   - The struct has a magic word and a sequence counter, so the host can
 *     tell "never written" from "written and then the kernel died".
 *
 * Read it from the host with tools/dbg/diag_read.py.
 */
#include "bsp_diag.h"

#include <rtthread.h>

#include "bsp_uart.h"

/* Kernel internals.  RT-Thread Nano exports these at file scope but declares
 * them in no public header, so a diagnostic module has to reach for them
 * directly.  That is acceptable here and only here: this is a debug facility,
 * it runs in the most conservative way possible (plain stores in the tick
 * ISR), and nothing in the product path depends on it.  If upstream ever
 * renames them, the build breaks loudly - which is the behaviour we want. */
extern rt_uint32_t rt_thread_ready_priority_group;
extern struct rt_thread *rt_current_thread;

/* Lives in .bss.  The host reader resolves this symbol from the *pinned* ELF
   it flashed, so it must never hardcode the address: unrelated symbols shift
   by a few bytes on every rebuild, and a stale address silently reads some
   other variable (which looks exactly like memory corruption). */
volatile bsp_diag_t g_bsp_diag;

#define BSP_DIAG_MAGIC      (0x52413844UL)   /* 'RA8D' */

void bsp_diag_mark (uint32_t code)
{
    g_bsp_diag.last_code = code;
    g_bsp_diag.last_code_tick = rt_tick_get();
}

void bsp_diag_tick (void)
{
    uint32_t seq = g_bsp_diag.seq + 1U;

    if (0U == g_bsp_diag.magic)
    {
        g_bsp_diag.magic = BSP_DIAG_MAGIC;
    }

    g_bsp_diag.rt_tick_now  = rt_tick_get();
    g_bsp_diag.ready_group  = rt_thread_ready_priority_group;
    g_bsp_diag.cur_thread   = (uint32_t) (uintptr_t) rt_current_thread;
    g_bsp_diag.int_nest     = rt_interrupt_get_nest();
    g_bsp_diag.rx_pending   = bsp_uart_rx_pending();

    /* Track the extremes: a single instantaneous sample of ready_group is
       meaningless (it is legitimately 0-adjacent at some points in the tick),
       but "has it EVER been non-zero" and "how often is it zero" are
       unambiguous. */
    if (0U == rt_thread_ready_priority_group)
    {
        g_bsp_diag.grp_zero_cnt++;
    }
    else
    {
        if (rt_thread_ready_priority_group > g_bsp_diag.grp_max)
        {
            g_bsp_diag.grp_max = rt_thread_ready_priority_group;
        }
    }

    if (0U == g_bsp_diag.grp_min)
    {
        g_bsp_diag.grp_min = rt_thread_ready_priority_group;
    }
    else if (rt_thread_ready_priority_group < g_bsp_diag.grp_min)
    {
        g_bsp_diag.grp_min = rt_thread_ready_priority_group;
    }

    g_bsp_diag.seq = seq;
}
