/**
  ******************************************************************************
  * @file    syscalls.c
  * @brief   Minimal newlib syscall stubs + printf retarget to USART1 (PA9/PA10).
  *
  *  printf() is wired to the debug UART through _write().  The link uses
  *  -u _printf_float only when floating point output is needed; the default
  *  build keeps newlib-nano's integer-only formatter to save flash.
  ******************************************************************************
  */
#include "main.h"

#include <errno.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <sys/unistd.h>

/* Provided by the linker script */
extern char end;      /* start of the heap  */
extern char _estack;  /* top of the stack   */

#ifndef UART_TX_TIMEOUT
#define UART_TX_TIMEOUT 100U
#endif

/* ---------------------------------------------------------------------------
 * Character I/O
 * -------------------------------------------------------------------------*/

__attribute__((weak)) int _write(int file, char *ptr, int len)
{
    (void)file;

    if (ptr == NULL || len <= 0)
    {
        return 0;
    }

    if (HAL_UART_Transmit(&huart1, (uint8_t *)ptr, (uint16_t)len,
                          UART_TX_TIMEOUT) != HAL_OK)
    {
        return -1;
    }
    return len;
}

__attribute__((weak)) int _read(int file, char *ptr, int len)
{
    (void)file;

    if (ptr == NULL || len <= 0)
    {
        return 0;
    }

    /* Blocking single character read - enough for a simple console */
    if (HAL_UART_Receive(&huart1, (uint8_t *)ptr, 1, HAL_MAX_DELAY) != HAL_OK)
    {
        return -1;
    }
    return 1;
}

/* ---------------------------------------------------------------------------
 * Heap
 * -------------------------------------------------------------------------*/

__attribute__((weak)) void *_sbrk(ptrdiff_t incr)
{
    static char *heap_end = NULL;
    char        *prev_heap_end;

    if (heap_end == NULL)
    {
        heap_end = &end;
    }

    prev_heap_end = heap_end;

    /* Refuse to grow into the stack */
    if ((heap_end + incr) > &_estack)
    {
        errno = ENOMEM;
        return (void *)-1;
    }

    heap_end += incr;
    return (void *)prev_heap_end;
}

/* ---------------------------------------------------------------------------
 * Stubs required to satisfy the linker
 * -------------------------------------------------------------------------*/

__attribute__((weak)) int _close(int file)
{
    (void)file;
    return -1;
}

__attribute__((weak)) int _fstat(int file, struct stat *st)
{
    (void)file;
    st->st_mode = S_IFCHR;
    return 0;
}

__attribute__((weak)) int _isatty(int file)
{
    (void)file;
    return 1;
}

__attribute__((weak)) int _lseek(int file, int ptr, int dir)
{
    (void)file;
    (void)ptr;
    (void)dir;
    return 0;
}

__attribute__((weak)) int _getpid(void)
{
    return 1;
}

__attribute__((weak)) int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}

__attribute__((weak)) void _exit(int status)
{
    (void)status;
    while (1)
    {
    }
}
