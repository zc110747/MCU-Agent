//////////////////////////////////////////////////////////////////////////////
//  (c) copyright 2023-by Persional Inc.  
//  All Rights Reserved
//
//  Name:
//      drv_usb.c
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
#include "drv_usb.h"
#include "usbd_cdc_if.h"

extern USBD_HandleTypeDef hUsbDeviceFS;

#define USB_DATA_SIZE   128
static uint8_t usb_data[USB_DATA_SIZE];

int usb_printf(const char *fmt, ...) 
{
    va_list args;
    int len;
    
    va_start(args, fmt);

    len = vsnprintf((char *)usb_data, USB_DATA_SIZE,  fmt, args);
    
    if (len > 0)
    {
        if(CDC_Transmit_FS(usb_data, len) != USBD_OK)
        {
            len = 0;
        }
    }
    
    va_end(args);
    return len;
}

int usb_send_buffer(uint8_t* buf, uint16_t len)
{
    if(CDC_Transmit_FS(buf, len) != USBD_OK)
    {
        len = 0;
    }
    return len;
}    

int usb_get_connected(void)
{
    if (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED) {
        return 1;
    } else {
        return 0;
    }
}
