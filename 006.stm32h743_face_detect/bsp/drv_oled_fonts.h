//////////////////////////////////////////////////////////////////////////////
//  (c) copyright 2026-by Persional Inc.  
//  All Rights Reserved
//
//  Name:
//      drv_oled_fonts.c
//
//  Purpose:
//      oled drivers with interface spi.
//
//  Author:
//      @公众号：<嵌入式技术总结>
//
//  Assumptions:
//	
//  Revision History:
//
/////////////////////////////////////////////////////////////////////////////
#ifndef __OLED_FONTS_H
#define __OLED_FONTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// 字体相关结构定义
typedef struct
{    
	const uint8_t 		*pTable;  		//	字模数组地址
	uint16_t 			Width; 		 	//	单个字符的字模宽度
	uint16_t 			Height; 			//	单个字符的字模长度
	uint16_t 			Sizes;	 		//	单个字符的字模数据个数
	uint16_t			Table_Rows;		// 该参数只有汉字字模用到，表示二维数组的行大小
} pFONT;

/*------------------------------------ 中文字体 ---------------------------------------------*/
extern	pFONT	CH_Font12 ;		//	1212字体
extern	pFONT	CH_Font16 ;    //	1616字体
extern	pFONT	CH_Font20 ;    //	2020字体
extern	pFONT	CH_Font24 ;    //	2424字体
extern	pFONT	CH_Font32 ;    //	3232字体


/*------------------------------------ ASCII字体 ---------------------------------------------*/
extern pFONT ASCII_Font32;		// 3216 字体
extern pFONT ASCII_Font24;		// 2412 字体
extern pFONT ASCII_Font20; 	// 2010 字体
extern pFONT ASCII_Font16; 	// 1608 字体
extern pFONT ASCII_Font12; 	// 1206 字体

#ifdef __cplusplus
}
#endif
#endif 
