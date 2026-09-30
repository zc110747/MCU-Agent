/**
 * @file    syscalls.c
 * @brief   Minimal newlib syscall stubs (heap + process only).
 *
 * NOTE: _write() is deliberately NOT implemented here any more. printf() is
 * retargeted to the non-blocking USART1 console in bsp/bsp_log.c, which owns
 * the single definition of _write(). The previous ITM/SWO stub was removed so
 * that printf() and PRINT_LOG() share one physical console.
 */
#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/times.h>

/* Provided by the linker script. */
extern char end asm("end");     /* first address after .bss */
extern char _estack;
extern unsigned int _Min_Stack_Size;

static char *heap_ptr;

/* --------------------------------------------------------------- heap */
void *_sbrk(ptrdiff_t incr)
{
    char *prev;
    const char *stack_limit = &_estack - (uintptr_t)&_Min_Stack_Size;

    if (heap_ptr == NULL)
    {
        heap_ptr = &end;
    }

    prev = heap_ptr;
    if (heap_ptr + incr > stack_limit)
    {
        errno = ENOMEM;
        return (void *)-1;
    }
    heap_ptr += incr;
    return prev;
}

/* --------------------------------------------------------------- I/O */
int _read(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    (void)len;
    return 0;
}

int _close(int file)
{
    (void)file;
    return -1;
}

int _fstat(int file, struct stat *st)
{
    (void)file;
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int file)
{
    (void)file;
    return 1;
}

int _lseek(int file, int ptr, int dir)
{
    (void)file;
    (void)ptr;
    (void)dir;
    return 0;
}

/* --------------------------------------------------------------- process */
void _exit(int status)
{
    (void)status;
    for (;;)
    {
    }
}

int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}

int _getpid(void)
{
    return 1;
}
