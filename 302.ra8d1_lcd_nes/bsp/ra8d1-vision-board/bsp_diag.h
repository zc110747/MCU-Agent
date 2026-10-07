/**
 * @file bsp_diag.h
 * @brief Crash-proof kernel liveness snapshot, readable over SWD.
 *
 * See bsp_diag.c for the rationale.  The host reader must resolve the
 * g_bsp_diag symbol from the same ELF it flashed - symbol addresses shift by
 * a few bytes whenever the firmware is rebuilt, and a stale hardcoded address
 * silently reads a neighbouring variable.
 */
#ifndef BSP_DIAG_H
#define BSP_DIAG_H

#include <stdint.h>

#define BSP_DIAG_MAGIC      (0x52413844UL)   /* 'RA8D' */

typedef struct
{
    uint32_t magic;         /**< BSP_DIAG_MAGIC once tick() has run        */
    uint32_t seq;           /**< increments every SysTick                  */
    uint32_t rt_tick_now;   /**< kernel tick at the last snapshot          */
    uint32_t ready_group;   /**< rt_thread_ready_priority_group            */
    uint32_t cur_thread;    /**< rt_current_thread, as an address          */
    uint32_t int_nest;      /**< rt_interrupt_get_nest()                   */
    uint32_t reserved_;     /**< keep the layout stable for host parsers   */
    uint32_t rx_pending;    /**< bytes waiting in the UART RX ring         */
    uint32_t last_code;     /**< app-supplied marker (bsp_diag_mark)       */
    uint32_t last_code_tick;/**< tick when the marker was set              */
    uint32_t grp_min;       /**< min ready_group ever seen (0 => broken)   */
    uint32_t grp_max;       /**< max ready_group ever seen                 */
    uint32_t grp_zero_cnt;  /**< ticks where ready_group == 0              */
} bsp_diag_t;

extern volatile bsp_diag_t g_bsp_diag;

/** Snapshot the kernel, called from the SysTick handler. */
void bsp_diag_tick (void);

/** Record an application progress marker (boot stage, failing step, ...). */
void bsp_diag_mark (uint32_t code);

#endif /* BSP_DIAG_H */
