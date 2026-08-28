#include <stdio.h>
#include <errno.h>

#if defined(__CC_ARM)
#pragma import(__use_no_semihosting)

struct __FILE 
{ 
	int handle; 
};
#else
//// _sys_command_string(BKPT)
//__asm(".global __ARM_use_no_argv");
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
    return ch;
}
