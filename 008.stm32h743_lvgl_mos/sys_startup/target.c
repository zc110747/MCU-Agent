/*
 * target.c - ARMCC / ARMCLANG semihosting suppression for Keil MDK-ARM.
 *
 * This file is the project's equivalent of the usual MDK-ARM/mdk_target.c and
 * MUST stay in the uvprojx (Application/Core group, ../sys_startup/target.c).
 *
 * WHY:
 *   bsp/jpeg_enc.c contains an active fprintf(stderr, ...) debug macro. If the
 *   C-library semihosting backend gets linked in, that call reaches
 *   _sys_open/_sys_write which execute a BKPT instruction -> the CPU stops dead
 *   at run time (the build itself still reports 0 errors). Suppressing
 *   semihosting is therefore mandatory, for BOTH compilers:
 *     - AC5 (__CC_ARM)    : #pragma import(__use_no_semihosting)
 *     - AC6 (ARMCLANG)    : __asm(".global __use_no_semihosting") / __ARM_use_no_argv
 *   The AC6 branch used to be commented out, which meant an ARMCLANG build had
 *   NO suppression at all while still linking semihosting. Re-enabled here.
 *
 * GCC does not need this: bsp/bsp_console.c supplies a strong _write() there.
 */

#include <stdio.h>
#include <errno.h>

#if defined(__CC_ARM)
#pragma import(__use_no_semihosting)

struct __FILE
{
	int handle;
};
#else
__asm(".global __use_no_semihosting");

// _sys_command_string(BKPT)
__asm(".global __ARM_use_no_argv");
#endif

void _sys_exit(int x)
{
	x = x;
}

void _ttywrch(int ch)
{
    ch = ch;
}

int fputc(int ch, FILE *f)
{
    (void)f;
    return ch;
}
