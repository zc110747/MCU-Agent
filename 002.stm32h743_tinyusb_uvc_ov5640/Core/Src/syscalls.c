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

#include "stm32h7xx_hal.h"

/* The newlib/sys headers <sys/stat.h> / <sys/types.h> are not provided by the
 * Keil ARMCLANG bare-metal runtime. _fstat() only needs a character-device
 * mode, so a minimal local definition keeps this file portable across both
 * the GCC/newlib and the Keil toolchains. */
#ifndef S_IFCHR
#define S_IFCHR 0020000
#endif
struct stat { unsigned int st_mode; };

/* Heap base/limit symbols differ between toolchains:
 *   GCC / newlib  : provided by the GNU ld script (_end, _estack, _Min_Stack_Size)
 *   Keil / ARMCLANG: provided by the MDK startup + scatter (__HeapBase, __HeapLimit) */
#if defined(__ARMCC_VERSION) || defined(__CC_ARM)
extern uint8_t __HeapBase;
extern uint8_t __HeapLimit;
#define HEAP_START  (&__HeapBase)
#define HEAP_LIMIT  (&__HeapLimit)
#else
extern uint8_t _end;
extern uint8_t _estack;
extern uint32_t _Min_Stack_Size;
#define HEAP_START  (&_end)
#define HEAP_LIMIT  ((uint8_t *)(&_estack - _Min_Stack_Size))
#endif

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
