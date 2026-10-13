/*
 * mdk_target.c - ARMCC / ARMCLANG semihosting suppression for Keil MDK-ARM.
 *
 * WHY THIS FILE MUST STAY IN THE uvprojx (Application/User group):
 *   The Keil build links against MicroLIB (useUlib=1).  app/log.c formats
 *   every PRINT_LOG() line with vsnprintf(), and LVGL's lv_snprintf() also
 *   ends up in the library stdio path.  Without semihosting suppression
 *   ARMCLANG links the C-library semihosting backend, whose _sys_open /
 *   _sys_write / _sys_command_string execute BKPT instructions -> the CPU
 *   stops dead at run time when no debugger is attached (the build itself
 *   still reports 0 errors).  Keep this file, always.
 *
 *   The fputc retarget at the bottom routes the library printf family to
 *   the project's own UART path (bsp_uart.c uart_write()), which also
 *   prevents the linker error
 *     L6915E: __use_no_semihosting was requested, but a semihosting fputc
 *     was linked in.
 *   Note that the application itself never calls printf() directly: it
 *   formats into a stack buffer and calls uart_write() with an explicit
 *   length (see app/log.c), so this retarget only covers the library's
 *   internal stdio users.
 *
 * GCC does not need it: app/syscalls.c supplies a strong _write() there
 * (and syscalls.c is deliberately NOT part of the Keil project).
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
#if !defined(__OPTIMIZE__)
__asm(".global __use_no_semihosting");

__asm(".global __ARM_use_no_argv");
#endif

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

/* Non-semihosting stdout retarget (see file header). */
int fputc(int ch, FILE *f)
{
    (void)f;
    extern int uart_write(const unsigned char *data, int len);
    unsigned char c = (unsigned char)ch;
    (void)uart_write(&c, 1);
    return ch;
}
