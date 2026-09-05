/* Minimal newlib system calls.
 *
 * This firmware does not print to a debug UART; printf() output (if any) is
 * redirected to the USB CDC port so that a stray library call cannot hang the
 * main loop. The heap is unused - every buffer is statically allocated. */

#include <sys/types.h>
#include <errno.h>
#include <stdint.h>

#ifndef __weak
#define __weak __attribute__((weak))
#endif

void *_sbrk(ptrdiff_t incr) {
  extern char end;              /* set by the linker script                */
  static char *heap_end;
  char *prev_heap_end;

  if (heap_end == 0) heap_end = &end;
  prev_heap_end = heap_end;
  heap_end += incr;
  return (void *) prev_heap_end;
}

__weak int _write(int file, char *ptr, int len) {
  (void) file;
  (void) ptr;
  return len;                   /* discard - nothing to print to           */
}

__weak int _read(int file, char *ptr, int len) {
  (void) file;
  (void) ptr;
  (void) len;
  errno = ENOSYS;
  return -1;
}

__weak int _close(int file) { (void) file; return -1; }
__weak int _fstat(int file, void *st) { (void) file; (void) st; return -1; }
__weak int _isatty(int file) { (void) file; return 1; }
__weak int _lseek(int file, int ptr, int dir) {
  (void) file; (void) ptr; (void) dir; return 0;
}
__weak void _exit(int status) { (void) status; while (1) { } }
__weak int _kill(int pid, int sig) { (void) pid; (void) sig; return -1; }
__weak int _getpid(void) { return 1; }
