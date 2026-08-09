//////////////////////////////////////////////////////////////////////////////
//  (c) copyright 2026-by Persional Inc.  
//  All Rights Reserved
//
//  Name:
//      drv_dac.h
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
#ifndef __DRV_DAC_H
#define __DRV_DAC_H

#include "main.h"

#define DAC_REFERENCE_VOL           3300
#define DAC_MAX_VALUE               4095

void drv_dac_set_value(uint16_t v_mv);
#endif
