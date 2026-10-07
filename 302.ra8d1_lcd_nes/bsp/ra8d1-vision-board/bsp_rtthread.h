/**
 * @file bsp_rtthread.h
 * @brief RT-Thread Nano board port - helpers beyond the three kernel hooks.
 */
#ifndef BSP_RTTHREAD_H_
#define BSP_RTTHREAD_H_

/** Enable the locked rt_kprintf override (see bsp_rtthread.c).
 *
 *  Must be called from a running thread - it takes a mutex, which requires
 *  the scheduler.  Until it is called, rt_kprintf still works but is not
 *  serialised. */
void bsp_console_lock_init (void);

#endif /* BSP_RTTHREAD_H_ */
