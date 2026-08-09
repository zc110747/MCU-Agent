//////////////////////////////////////////////////////////////////////////////
//  (c) copyright 2026-by Persional Inc.  
//  All Rights Reserved
//
//  Name:
//      drv_dcmi_ov5640.c
//
//  Purpose:
//      dcmi interface.
//
//  Author:
//      @公众号：<嵌入式技术总结>
//
//  Assumptions:
//	
//  Revision History:
//
/////////////////////////////////////////////////////////////////////////////
#ifndef __DRV_DCMI_OV5640_H
#define __DRV_DCMI_OV5640_H

#include "main.h"

GlobalType_t drv_dcmi_ov5640_download_firmware(void);
void drv_dcmi_trigger_constant(void);
GlobalType_t drv_dcmi_ov5640_init(void);
#endif