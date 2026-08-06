//////////////////////////////////////////////////////////////////////////////
//  (c) copyright 2026-by Persional Inc.  
//  All Rights Reserved
//
//  Name:
//      drv_spi_oled.h
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
#ifndef __SPI_OLED_H
#define __SPI_OLED_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "drv_oled_fonts.h"

// global defined
#define LCD_WIDTH               240		// LCD的像素长度
#define LCD_HEIGHT              240		// LCD的像素宽度

// direction
#define	Direction_H				0		// LCD横屏显示
#define	Direction_H_Flip	    1		// LCD横屏显示,上下翻转
#define	Direction_V				2		// LCD竖屏显示 
#define	Direction_V_Flip	    3		// LCD竖屏显示,上下翻转 

// colors
#define 	LCD_WHITE       0xFFFFFF	// 纯白色
#define 	LCD_BLACK       0x000000    // 纯黑色
                        
#define 	LCD_BLUE        0x0000FF	 //	纯蓝色
#define 	LCD_GREEN       0x00FF00    //	纯绿色
#define 	LCD_RED         0xFF0000    //	纯红色
#define 	LCD_CYAN        0x00FFFF    //	蓝绿色
#define 	LCD_MAGENTA     0xFF00FF    //	紫红色
#define 	LCD_YELLOW      0xFFFF00    //	黄色
#define 	LCD_GREY        0x2C2C2C    //	灰色
												
#define 	LIGHT_BLUE      0x8080FF    //	亮蓝色
#define 	LIGHT_GREEN     0x80FF80    //	亮绿色
#define 	LIGHT_RED       0xFF8080    //	亮红色
#define 	LIGHT_CYAN      0x80FFFF    //	亮蓝绿色
#define 	LIGHT_MAGENTA   0xFF80FF    //	亮紫红色
#define 	LIGHT_YELLOW    0xFFFF80    //	亮黄色
#define 	LIGHT_GREY      0xA3A3A3    //	亮灰色
												
#define 	DARK_BLUE       0x000080    //	暗蓝色
#define 	DARK_GREEN      0x008000    //	暗绿色
#define 	DARK_RED        0x800000    //	暗红色
#define 	DARK_CYAN       0x008080    //	暗蓝绿色
#define 	DARK_MAGENTA    0x800080    //	暗紫红色
#define 	DARK_YELLOW     0x808000    //	暗黄色
#define 	DARK_GREY       0x404040    //	暗灰色

// 设置变量显示时多余位补0还是补空格
// 只有 LCD_DisplayNumber() 显示整数 和 LCD_DisplayDecimals()显示小数 这两个函数用到
// 使用示例： LCD_ShowNumMode(Fill_Zero) 设置多余位填充0，例如 123 可以显示为 000123
#define  Fill_Zero  0		//填充0
#define  Fill_Space 1		//填充空格

// global type defined
typedef struct	//LCD相关参数结构体
{
    uint32_t Color;  				//LCD当前画笔颜色
    uint32_t BackColor;			    //背景色
    uint8_t  ShowNum_Mode;		    // 数字显示模式
    uint8_t  Direction;			    //	显示方向
    uint16_t Width;                 // 屏幕像素长度
    uint16_t Height;                // 屏幕像素宽度	
    uint8_t  X_Offset;              // X坐标偏移，用于设置屏幕控制器的显存写入方式
    uint8_t  Y_Offset;              // Y坐标偏移，用于设置屏幕控制器的显存写入方式
}OLED_INFO;

GlobalType_t driver_spi_oled_init(void);

void LCD_SetDirection(uint8_t direction);
void LCD_SetBackColor(uint32_t Color);
void LCD_SetColor(uint32_t Color);
void LCD_Clear(void);
void LCD_SetAsciiFont(pFONT *Asciifonts);
void LCD_ShowNumMode(uint8_t mode);
void LCD_SetTextFont(pFONT *fonts);
void LCD_DisplayText(uint16_t x, uint16_t y, char *pText);
void  LCD_DisplayNumber( uint16_t x, uint16_t y, int32_t number, uint8_t len);
void LCD_CopyBuffer(uint16_t x, uint16_t y,uint16_t width,uint16_t height,uint16_t *DataBuff);

#ifdef __cplusplus
}
#endif

#endif
