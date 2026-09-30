/*
 * mdk_target.c - ARMCC / ARMCLANG semihosting suppression for Keil MDK-ARM.
 *
 * WHY THIS FILE MUST STAY IN THE uvprojx (Application/User/Core group):
 *   app/lwip/arch/cc.h defines LWIP_PLATFORM_ASSERT as an active printf(...)
 *   call. Without semihosting suppression ARMCLANG links the C-library
 *   semihosting backend, and that printf reaches _sys_open/_sys_write which
 *   execute a BKPT instruction -> the CPU stops dead at run time (the build
 *   itself still reports 0 errors). Keep this file, always.
 *
 * GCC does not need it: app/syscalls.c supplies a strong _write() there.
 */

#include <stdio.h>

#if defined(__CC_ARM)
/* ---- ARMCC 5 (AC5) ---- */
#pragma import(__use_no_semihosting)

struct __FILE
{
	int handle;
};
#else
/* ---- ARMCLANG (AC6) ----
 * NOTE: __ARM_use_no_argv is given a REAL definition here, not just a .global
 * declaration: ARMCLANG 6.14 does not emit the (weak) symbol itself, so a bare
 * `.global` leaves the linker reporting
 *   "Undefined symbol __ARM_use_no_argv (referred from mdk_target.o)".
 * Declaring it as a global data object both satisfies the reference and keeps
 * _sys_command_string (which contains a BKPT) out of the image.
 */
__asm(".global __use_no_semihosting");
int __ARM_use_no_argv = 0;
#endif

FILE __stdout;
void _sys_exit(int x)
{
	x = x;
}

void _ttywrch(int ch)
{
    ch = ch;
}

/* Non-semihosting stdout retarget.
 * app/lwip/arch/cc.h routes LWIP_PLATFORM_ASSERT through printf(), so the C
 * library's printf needs an fputc to write through. Without this definition
 * the library's own (semihosting) fputc is pulled in and the linker aborts:
 *   L6915E: __use_no_semihosting was requested, but a semihosting fputc was linked in
 * Routing it to the project's own UART path instead keeps semihosting out of
 * the image while preserving the assertion diagnostics. */
int fputc(int ch, FILE *f)
{
    (void)f;
    extern int uart_write(const unsigned char *data, int len);
    unsigned char c = (unsigned char)ch;
    (void)uart_write(&c, 1);
    return ch;
}

