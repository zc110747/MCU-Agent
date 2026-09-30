/**
 * @file rtconfig.h
 * @brief RT-Thread Nano (5.0.2) configuration for RA8D1 Vision Board
 *
 * Scope (Phase 1): kernel + console + finsh/msh.
 * Deliberately OFF: device framework, DFS, libc/POSIX, libc hooks —
 * rt_kprintf() goes straight to rt_hw_console_output() (see bsp_uart.c).
 */
#ifndef RT_CONFIG_H__
#define RT_CONFIG_H__

/* ------------------------------------------------------------ kernel */
#define RT_NAME_MAX                 12
#define RT_ALIGN_SIZE               8
#define RT_THREAD_PRIORITY_32
#define RT_THREAD_PRIORITY_MAX      32
#define RT_TICK_PER_SECOND          1000
#define RT_USING_OVERFLOW_CHECK
#define IDLE_THREAD_STACK_SIZE      256

/* -------------------------------------------- inter-thread communication */
#define RT_USING_SEMAPHORE

/* ------------------------------------------------- memory management */
/* small-memory allocator over a static array in .bss (see bsp_rtthread.c) */
#define RT_USING_HEAP
#define RT_USING_SMALL_MEM
#define RT_USING_SMALL_MEM_AS_HEAP

/* ------------------------------------------------------------ console */
#define RT_USING_CONSOLE
#define RT_CONSOLEBUF_SIZE          128

/* --------------------------------------------------------- components */
#define RT_USING_COMPONENTS_INIT
#define RT_USING_USER_MAIN
#define RT_MAIN_THREAD_STACK_SIZE   2048
#define RT_MAIN_THREAD_PRIORITY     10

/* --------------------------------------------------------- arch / cpu */
#define RT_USING_HW_ATOMIC
#define RT_USING_CPU_FFS
#define ARCH_ARM
#define ARCH_ARM_CORTEX_M
#define ARCH_ARM_CORTEX_M85

/* -------------------------------------------------------------- finsh */
#define RT_USING_FINSH
#define FINSH_USING_MSH
#define FINSH_THREAD_NAME           "tshell"
#define FINSH_THREAD_PRIORITY       20
#define FINSH_THREAD_STACK_SIZE     4096
#define FINSH_USING_SYMTAB
#define FINSH_CMD_SIZE              80
#define MSH_USING_BUILT_IN_COMMANDS
#define FINSH_USING_DESCRIPTION
#define FINSH_ARG_MAX               10

#endif /* RT_CONFIG_H__ */
