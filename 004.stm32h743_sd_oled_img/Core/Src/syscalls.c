/**
  ******************************************************************************
  * @file    syscalls.c
  * @brief   Minimal newlib syscall stubs for a bare-metal target.
  *
  * _write() is intentionally NOT defined here: bsp_log.c provides it so that
  * printf() ends up on USART1.
  ******************************************************************************
  */

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>

/* Provided by the linker script */
extern uint8_t _end;      /* start of the heap  */
extern uint8_t _estack;   /* top of the stack   */
extern uint32_t _Min_Stack_Size;

static uint8_t *__sbrk_heap_end = NULL;

/**
  * @brief  Allocate memory for newlib's malloc().
  */
void *_sbrk(ptrdiff_t incr)
{
    const uint8_t  *max_heap = (uint8_t *)((uint32_t)&_estack - (uint32_t)&_Min_Stack_Size);
    uint8_t        *prev_heap_end;

    if (__sbrk_heap_end == NULL) {
        __sbrk_heap_end = &_end;
    }

    if (__sbrk_heap_end + incr > max_heap) {
        errno = ENOMEM;
        return (void *)-1;
    }

    prev_heap_end     = __sbrk_heap_end;
    __sbrk_heap_end  += incr;

    return (void *)prev_heap_end;
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

int _read(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    (void)len;
    return 0;
}

int _getpid(void)
{
    return 1;
}

int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}

void _exit(int status)
{
    (void)status;
    while (1) {
    }
}
