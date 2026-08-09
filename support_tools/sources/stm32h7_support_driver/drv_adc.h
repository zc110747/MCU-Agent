//////////////////////////////////////////////////////////////////////////////
//  (c) copyright 2026-by Persional Inc.  
//  All Rights Reserved
//
//  Name:
//      drv_adc.h
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
#ifndef _DRV_ADC_H_
#define _DRV_ADC_H_ 

#include "main.h"

#define ADC_REF_MV          3300
#define ADC_RESOLUTION      12  

GlobalType_t driver_adc_init(void);
uint32_t drv_adc_get_mv(void);
#endif
