/**
 ******************************************************************************
 * @file    syscalls.c
 * @brief   Minimal newlib-nano syscall stubs.
 *
 * We link with --specs=nosys.specs, which already supplies weak stubs, but
 * providing our own keeps the linker quiet about the missing heap symbols and
 * lets printf() be redirected later (e.g. to ITM/SWO) without touching newlib.
 ******************************************************************************
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "stm32h7xx_hal.h"

/* Provided by the linker script */
extern uint8_t _end;   /* start of the heap             */
extern uint8_t _estack;
extern uint32_t _Min_Stack_Size;

/* --------------------------------------------------------------------------
 * Heap
 * -------------------------------------------------------------------------- */
void *_sbrk(ptrdiff_t incr)
{
  static uint8_t *heap_end = NULL;

  const uint8_t *stack_limit = &_estack - (uintptr_t)&_Min_Stack_Size;

  if (heap_end == NULL) {
    heap_end = &_end;
  }

  uint8_t *prev = heap_end;

  if (heap_end + incr > stack_limit) {
    errno = ENOMEM;
    return (void *)-1;
  }

  heap_end += incr;
  return (void *)prev;
}

/* --------------------------------------------------------------------------
 * stdio - printf() output is discarded for now. Redirect here (ITM, UART, ...)
 * when you need a console.
 * -------------------------------------------------------------------------- */
__attribute__((weak)) int _write(int file, char *ptr, int len)
{
  (void)file;

#if defined(DEBUG_ITM_PRINTF)
  for (int i = 0; i < len; i++) {
    ITM_SendChar((uint32_t)ptr[i]);
  }
#else
  (void)ptr;
#endif

  return len;
}

__attribute__((weak)) int _read(int file, char *ptr, int len)
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
