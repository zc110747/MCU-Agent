
#include <stdio.h>

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

FILE __stdout;     
void _sys_exit(int x) 
{ 
	x = x; 
}

void _ttywrch(int ch)
{
    ch = ch;
}