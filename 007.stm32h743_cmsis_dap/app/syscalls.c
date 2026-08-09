/* ---------------------------------------------------------------------------
 * Minimal newlib glue.
 *
 * _sbrk is a real implementation backed by the heap region the linker script
 * reserves. The rest are deliberate no-ops: defining them here (rather than
 * letting nosys.specs supply them) keeps the link log free of the
 * "_xxx is not implemented and will always fail" warnings.
 *
 * _write() lives in main.c, where it redirects printf() to the USB CDC port.
 * -------------------------------------------------------------------------*/

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

extern char end;      /* first free byte after .bss - from the linker script */
extern char _estack;
extern unsigned long _Min_Stack_Size;

static char* heap_ptr = 0;

void* _sbrk(ptrdiff_t incr) {
  if (heap_ptr == 0) {
    heap_ptr = &end;
  }

  /* Keep the heap from growing into the space reserved for the stack. */
  char* const limit = (char*) &_estack - (ptrdiff_t) &_Min_Stack_Size;
  char* const prev  = heap_ptr;

  if (prev + incr > limit) {
    errno = ENOMEM;
    return (void*) -1;
  }

  heap_ptr = prev + incr;
  return (void*) prev;
}

/* ------------------------------------------------------------------------ */
/* Stubs - there is no filesystem and no process model on this target.       */
/* ------------------------------------------------------------------------ */
int _close(int fd) {
  (void) fd;
  return -1;
}

int _fstat(int fd, struct stat* st) {
  (void) fd;
  st->st_mode = S_IFCHR;   /* everything looks like a character device */
  return 0;
}

int _isatty(int fd) {
  (void) fd;
  return 1;
}

int _lseek(int fd, int offset, int whence) {
  (void) fd; (void) offset; (void) whence;
  return 0;
}

int _read(int fd, char* ptr, int len) {
  (void) fd; (void) ptr; (void) len;
  return 0;                /* stdin is not wired up */
}

int _getpid(void) {
  return 1;
}

int _kill(int pid, int sig) {
  (void) pid; (void) sig;
  errno = EINVAL;
  return -1;
}

/* No console on a debug probe: discard anything printf()/write() would emit.
 * Defining it here (instead of letting libg_nano supply an stub that always
 * fails) keeps the link log clean. */
int _write(int fd, char* ptr, int len) {
  (void) fd; (void) ptr; (void) len;
  return len;
}
