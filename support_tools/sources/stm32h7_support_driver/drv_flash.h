//////////////////////////////////////////////////////////////////////////////
//  (c) copyright 2023-by Persional Inc.  
//  All Rights Reserved
//
//  Name:
//      drv_flash.h
//
//  Purpose:
//      driver for flash module.
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
#ifndef _DRV_FLASH_H
#define _DRV_FLASH_H

#include "main.h"

GlobalType_t flash_update_empty_device(uint32_t address, uint32_t sector_num);
GlobalType_t flash_update_write_device(uint32_t address, uint32_t *pbuffer, uint32_t size);
#endif
