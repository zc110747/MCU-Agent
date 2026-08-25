/**
  ******************************************************************************
  * @file    bsp/syscalls.c
  * @brief   Minimal newlib syscalls for a bare-metal STM32H7 application.
  *
  * We do NOT use newlib's stdio for output (BSP_UART_Printf talks to the UART
  * directly via HAL), but vsnprintf() still pulls in the C library, which needs
  * a few system calls to link. Providing them here lets us drop the toolchain's
  * "nosys" stubs (which spam "not implemented" link warnings) for a clean,
  * zero-warning build.
  *
  * Heap is carved out of AXI SRAM (SRAM1 @ 0x24000000, 512 KB), which is where
  * .bss/.data live in this project's linker script.
  ******************************************************************************
  */
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/unistd.h>

/* End of BSS (set by the linker script); heap grows up from here. */
extern char _ebss;
/* Top of AXI SRAM (512 KB region): 0x24000000 + 0x80000 */
#define HEAP_END  ((char *)0x24080000UL)

static char *s_heap_top = &_ebss;

/**
  * @brief  Increase the heap (called by malloc / newlib _sbrk_r).
  */
caddr_t _sbrk(int incr)
{
    char *prev = s_heap_top;
    if (s_heap_top + incr > HEAP_END) {
        errno = ENOMEM;
        return (caddr_t)-1;
    }
    s_heap_top += incr;
    return (caddr_t)prev;
}

int _close(int fd)        { (void)fd; return -1; }
int _read(int fd, char *ptr, int len) { (void)fd; (void)ptr; (void)len; return 0; }
int _write(int fd, const char *ptr, int len) { (void)fd; (void)ptr; (void)len; return len; }
int _fstat(int fd, struct stat *st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd)       { (void)fd; return 1; }
int _lseek(int fd, int ptr, int dir) { (void)fd; (void)ptr; (void)dir; return 0; }
int _kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
int _getpid(void)         { return -1; }

void _exit(int status)     { (void)status; while (1) {} }
