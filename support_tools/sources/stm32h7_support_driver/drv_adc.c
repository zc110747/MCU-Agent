//////////////////////////////////////////////////////////////////////////////
//  (c) copyright 2026-by Persional Inc.  
//  All Rights Reserved
//
//  Name:
//      drv_adc.c
//
//  Purpose:
//      adc interface.
//
//  Author:
//      @公众号：<嵌入式技术总结>
//
//  Assumptions:
//	
//  Revision History:
//
/////////////////////////////////////////////////////////////////////////////
#include "drv_adc.h"

extern ADC_HandleTypeDef hadc1;

#define ADC_VALUE_NUMS     10
uint32_t g_adc_value[ADC_VALUE_NUMS];
volatile uint32_t g_vol_value = 0;

GlobalType_t driver_adc_init(void)
{
    LL_ADC_SetCommonPathInternalCh(ADC12_COMMON, ADC_CCR_VREFEN);
    
    HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);
    
    HAL_ADC_Start_DMA(&hadc1, g_adc_value, ADC_VALUE_NUMS);
    return RT_OK;
}

static void calc_adc_value(void)
{
    uint32_t temp = 0;
    uint8_t index;
    
    for (index=0; index<ADC_VALUE_NUMS; index++)
    {
        temp += g_adc_value[index];
    }
    
    g_vol_value = temp/ADC_VALUE_NUMS;
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == hadc1.Instance) 
    {
        HAL_ADC_Stop_DMA(&hadc1);
        
        calc_adc_value();
        
        SCB_CleanInvalidateDCache_by_Addr(g_adc_value, ADC_VALUE_NUMS*sizeof(uint32_t));
        HAL_ADC_Start_DMA(&hadc1, g_adc_value, (uint32_t)(ADC_VALUE_NUMS));
    }
}

uint32_t drv_adc_get_mv(void)
{
    return g_vol_value*ADC_REF_MV>>ADC_RESOLUTION;
}
