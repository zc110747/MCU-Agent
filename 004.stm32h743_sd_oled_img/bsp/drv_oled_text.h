//////////////////////////////////////////////////////////////////////////////
//  (c) copyright 2023-by Persional Inc.  
//  All Rights Reserved
//
//  Name:
//      drv_lcd_text.h
//
//  Purpose:
//      lcd driver write interface.
//
// Author:
//      @公众号：<嵌入式技术总结>
//
//  Assumptions:
//
//  Revision History:
//
/////////////////////////////////////////////////////////////////////////////
#ifndef _DRV_OLED_TEXT_H
#define _DRV_OLED_TEXT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "drv_oled_fonts.h"

extern pFONT CH_TEXT_Font12;
extern pFONT CH_TEXT_Font16;
extern pFONT CH_TEXT_Font24;
extern pFONT CH_TEXT_Font32;

GlobalType_t lcd_driver_font_init(void);
GlobalType_t lcd_driver_get_hzmat(uint8_t *code, uint8_t *pbuffer, pFONT *font);

#ifdef __cplusplus
}
#endif
#endif
