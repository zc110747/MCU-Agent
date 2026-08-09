//////////////////////////////////////////////////////////////////////////////
//  (c) copyright 2026-by Persional Inc.  
//  All Rights Reserved
//
//  Name:
//      drv_dac.c
//
//  Purpose:
//      dac interface.
//
//  Author:
//      @公众号：<嵌入式技术总结>
//
//  Assumptions:
//	
//  Revision History:
//
/////////////////////////////////////////////////////////////////////////////
#include "drv_dac.h"

extern DAC_HandleTypeDef hdac1;

void drv_dac_set_value(uint16_t v_mv)
{
    float adc_value;
    
    if (v_mv > DAC_REFERENCE_VOL)
        v_mv = DAC_REFERENCE_VOL;

    adc_value = (float)v_mv/DAC_REFERENCE_VOL * DAC_MAX_VALUE;

    HAL_DAC_Stop(&hdac1, DAC_CHANNEL_1);
    HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, (uint32_t)adc_value);
    HAL_DAC_Start(&hdac1, DAC_CHANNEL_1); 
}
