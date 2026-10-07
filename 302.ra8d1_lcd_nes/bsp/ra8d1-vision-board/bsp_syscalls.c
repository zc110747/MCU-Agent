/**
 * @file bsp_syscalls.c
 * @brief Minimal newlib syscall stubs (nano.specs + nosys.specs cleanup)
 */
#include <stdint.h>
#include <errno.h>
#include <sys/stat.h>

extern int errno;

int _getpid(void)
{
    return 1;
}

int _kill(int pid, int sig)
{
    (void) pid;
    (void) sig;
    errno = EINVAL;
    return -1;
}

void _exit(int status)
{
    (void) status;
    while (1)
    {
    }
}

int _close(int file)
{
    (void) file;
    return -1;
}

int _fstat(int file, struct stat *st)
{
    (void) file;
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int file)
{
    (void) file;
    return 1;
}

int _lseek(int file, int ptr, int dir)
{
    (void) file;
    (void) ptr;
    (void) dir;
    return 0;
}

int _read(int file, char *buf, int len)
{
    (void) file;
    (void) buf;
    (void) len;
    return 0;
}

int _write(int file, const char *buf, int len)
{
    (void) file;
    (void) buf;
    return len;
}
