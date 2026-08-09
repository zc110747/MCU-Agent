//////////////////////////////////////////////////////////////////////////////
//  (c) copyright 2023-by Persional Inc.  
//  All Rights Reserved
//
//  Name:
//      drv_usb.h
//
//  Purpose:
//      driver for usb module.
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
#ifndef __DRV_USB_H
#define __DRV_USB_H

#include "main.h"

int usb_get_connected(void);
int usb_printf(const char *fmt, ...);
int usb_send_buffer(uint8_t* buf, uint16_t len);
#endif
