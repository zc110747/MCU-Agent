/**
  ******************************************************************************
  * @file    syscalls.c
  * @brief   Minimal newlib syscalls for a bare-metal STM32 (no OS).
  *          _write() is provided in bsp_uart.c (retarget to USART1).
  ******************************************************************************
  */
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>

extern char _end;            /* provided by the linker script */
static char *heap_end = 0;

void *_sbrk(int incr)
{
  char *prev_end;

  if (heap_end == 0)
  {
    heap_end = &_end;
  }
  prev_end = heap_end;
  heap_end += incr;
  return (void *)prev_end;
}

int _close(int fd)        { (void)fd; return -1; }
int _read(int fd, char *ptr, int len) { (void)fd; (void)ptr; (void)len; return 0; }
int _lseek(int fd, int ptr, int dir)  { (void)fd; (void)ptr; (void)dir; return 0; }
int _fstat(int fd, struct stat *st)   { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd)       { (void)fd; return 1; }
int _open(const char *path, int flags, int mode) { (void)path; (void)flags; (void)mode; return -1; }
void _exit(int status)    { (void)status; while (1) { } }
int _kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
int _getpid(void)         { return 1; }
