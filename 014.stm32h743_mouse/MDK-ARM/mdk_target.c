/*
 * mdk_target.c - ARMCLANG retarget stubs / semihosting suppression.
 *
 * WHY THIS FILE IS IN THE KEY PROJECT (Application/User group):
 *   Without a no-semihosting declaration, ARMCLANG may satisfy stdio from the
 *   semihosting backend. _sys_write / _sys_open in that backend execute a
 *   BKPT instruction, so the very first printf() stops the CPU dead at run
 *   time - while the build still reports 0 errors. The firmware itself prints
 *   nothing (the UART port answers in plain ASCII through
 *   protocol/command_handler.c), but the linker can still pull the backend in
 *   through the C library, so the suppression stays.
 *
 * The GCC build does not use this file: bsp/syscalls.c supplies the _write /
 * _sbrk stubs there, and the CMake target links with --specs=nosys.specs.
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
 * __ARM_use_no_argv gets a REAL definition here, not just a .global
 * declaration: ARMCLANG 6.14 does not emit the (weak) symbol itself, so a bare
 * `.global` leaves the linker reporting
 *   "Undefined symbol __ARM_use_no_argv (referred from mdk_target.o)".
 * Defining it as a global data object satisfies the reference and keeps
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
