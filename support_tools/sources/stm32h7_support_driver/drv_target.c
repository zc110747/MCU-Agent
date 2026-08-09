//////////////////////////////////////////////////////////////////////////////
//  (c) copyright 2023-by Persional Inc.  
//  All Rights Reserved
//
//  Name:
//      drv_target.c
//
//  Purpose:
//      support printf output.
//
//  Author:
//      @公众号：<嵌入式技术总结>
//
//  Assumptions:
//	
//
//  Revision History:
//
/////////////////////////////////////////////////////////////////////////////
#include <stdio.h>
#include "drv_target.h"

#if defined(RUN_OS_MODE) && RUN_OS_MODE == RUN_WITH_FREERTOS
#include "FreeRTOS.h"
#include "task.h"
#endif

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

//use MicroLIB.
volatile int32_t ITM_RxBuffer = ITM_RXBUFFER_EMPTY;
int fputc(int ch, FILE *f)
{  
    ITM_SendChar((uint32_t)ch);
    return ch;
}

static uint8_t is_os_on = 0;

void set_os_on(void)
{
    is_os_on = 1;
}

uint8_t get_os_on(void)
{
    return is_os_on;
}

void hal_delay_ms(uint16_t time)
{
    if(is_os_on == 0)
    {
        HAL_Delay(time);
    }
    else
    {
#if defined(RUN_OS_MODE) && RUN_OS_MODE == RUN_WITH_FREERTOS
        vTaskDelay(time);
#else
#endif
    }
}

uint32_t drv_tick_difference(uint32_t value, uint32_t now_tick)
{
    return (now_tick >= value) ? now_tick - value : UINT_MAX - value + now_tick + 1;
}

uint8_t bcdToDec(uint8_t val) 
{
	return ((val/16*10) + (val%16));
}

uint8_t decToBcd(uint8_t val) 
{
	return ( (val/10*16) + (val%10));
}

void delay_us(uint16_t times)
{
    uint16_t i, j;
    for(i=0; i<times; i++)
    {
        for(j=0; j<5; j++)
        {
            __NOP();
        }
    }
}

