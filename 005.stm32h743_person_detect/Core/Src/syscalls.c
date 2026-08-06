/**
 * @file    syscalls.c
 * @brief   Minimal newlib syscall stubs.
 *
 * printf() output is retargeted to the Cortex-M7 ITM stimulus port 0 so that
 * it shows up in the cortex-debug "SWO" console without needing a UART.
 */
#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/times.h>

#include "stm32h7xx_hal.h"

/* Provided by the linker script. */
extern char end asm("end");     /* first address after .bss */
extern char _estack;
extern unsigned int _Min_Stack_Size;

static char *heap_ptr;

/* --------------------------------------------------------------- I/O */
int _write(int file, char *ptr, int len)
{
    (void)file;
    for (int i = 0; i < len; i++)
    {
        ITM_SendChar((uint32_t)(uint8_t)ptr[i]);
    }
    return len;
}

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
